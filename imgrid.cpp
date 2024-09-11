
#include "imgrid.h"
#include "imgrid_grid_engine.h"
#include "imgrid_internal.h"

#include <limits.h>
#include <math.h>
#include <new>
#include <optional>
#include <stdint.h>
#include <stdio.h> // for fwrite, ssprintf, sscanf
#include <stdlib.h>
#include <string.h> // strlen, strncmp

// Use secure CRT function variants to avoid MSVC compiler errors
#ifdef _MSC_VER
#define sscanf sscanf_s
#endif

ImGridContext *GImGrid = NULL;

ImGridIO::MultipleSelectModifier::MultipleSelectModifier() : Modifier(NULL) {}

ImGridEntry::ImGridEntry(const int id)
    : Id(id), Origin(0, 0), Rect(), TitleBarContentRect(), GridData(this),
      Draggable(true), Resizable(true), Locked(false), Moving(false),
      PreviewRect(), HasPreview(false), PreviewHeld(false),
      PreviewHovered(false), CachedItemRect(), ColorStyle(), LayoutStyle() {}

ImGridStyle::ImGridStyle()
    : GridSpacing(5.f), EntryCornerRounding(4.f), EntryPadding(8.f, 8.f),
      EntryBorderThickness(1.f), Flags(ImGridStyleFlags_None), Colors() {}

namespace ImGrid {

namespace {

void Initialize(ImGridContext *ctx) {
  ctx->HoveredEntryIdx = -1;
  ctx->HoveredEntryTitleBarIdx = -1;
  ctx->CurrentScope = ImGridScope_None;

  StyleColorsDark();
}

inline ImVec2 ScreenSpaceToGridSpace(const ImGridContext &ctx,
                                     const ImVec2 &v) {
  return v + ctx.CanvasOriginScreenSpace - ctx.Panning;
}

inline ImRect ScreenSpaceToGridSpace(const ImGridContext &ctx,
                                     const ImRect &r) {
  return ImRect(ScreenSpaceToGridSpace(ctx, r.Min),
                ScreenSpaceToGridSpace(ctx, r.Max));
}

[[maybe_unused]] inline ImVec2 GridSpaceToScreenSpace(const ImGridContext &ctx,
                                                      const ImVec2 &v) {
  return v + ctx.CanvasOriginScreenSpace + ctx.Panning;
}

inline ImVec2 GridSpaceToSpace(const ImGridContext &ctx, const ImVec2 &v) {
  return v + ctx.Panning;
}

[[maybe_unused]] inline ImVec2 SpaceToGridSpace(const ImGridContext &ctx,
                                                const ImVec2 &v) {
  return v - ctx.Panning;
}

inline ImVec2 SpaceToScreenSpace(const ImVec2 &v) {
  return GImGrid->CanvasOriginScreenSpace + v;
}

inline ImVec2 GetEntryTitleBarOrigin(const ImGridEntry &node) {
  return node.Origin + node.LayoutStyle.Padding;
}

inline ImRect GetItemRect(ImGridContext &ctx, ImGridEntry &entry,
                          bool cache = false) {
  (void)cache;
  (void)entry;
  (void)ctx;

  // Retrieve the current item rectangle and its size
  ImRect rect = ImRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());

  return rect;
}

inline ImVec2 GetEntryContentOrigin(const ImGridEntry &node) {
  const ImVec2 title_bar_height =
      ImVec2(0.f, node.TitleBarContentRect.GetHeight() +
                      2.0f * node.LayoutStyle.Padding.y);
  auto orig = node.Origin + title_bar_height + node.LayoutStyle.Padding;
  return orig;
}

inline ImRect GetEntryTitleRect(const ImGridEntry &node) {
  ImRect expanded_title_rect = node.TitleBarContentRect;
  expanded_title_rect.Expand(node.LayoutStyle.Padding);

  return ImRect(expanded_title_rect.Min,
                expanded_title_rect.Min + ImVec2(node.Rect.GetWidth(), 0.f) +
                    ImVec2(0.f, expanded_title_rect.GetHeight()));
}

// SECTION[DrawLists]
// The draw list channels are structured as follows. First we have our base
// channel, the canvas grid on which we render the grid lines in
// BeginNodeEditor(). The base channel is the reason
// draw_list_submission_idx_to_background_channel_idx offsets the index by
// one. Each BeginEntry() call appends two new draw channels, for the entry
// background and foreground. The node foreground is the channel into which
// the node's ImGui content is rendered. Finally, in EndNodeEditor() we
// append one last draw channel for rendering the selection box and the
// incomplete link on top of everything else.
//
// +----------+----------+----------+----------+----------+----------+
// |          |          |          |          |          |          |
// |canvas    |node      |node      |...       |...       |click     |
// |grid      |background|foreground|          |          |interaction
// |          |          |          |          |          |          |
// +----------+----------+----------+----------+----------+----------+
//            |                     |
//            |   submission idx    |
//            |                     |
//            -----------------------

void DrawListSet(ImDrawList *window_draw_list) {
  GImGrid->CanvasDrawList = window_draw_list;
  GImGrid->EntryIdxToSubmissionIdx.Clear();
  GImGrid->EntryIdxSubmissionOrder.clear();
}

void ImDrawListGrowChannels(ImDrawList *draw_list, const int num_channels) {
  ImDrawListSplitter &splitter = draw_list->_Splitter;

  if (splitter._Count == 1) {
    splitter.Split(draw_list, num_channels + 1);
    return;
  }

  // NOTE: this logic has been lifted from ImDrawListSplitter::Split with
  // slight modifications to allow nested splits. The main modification is
  // that we only create new ImDrawChannel instances after splitter._Count,
  // instead of over the whole splitter._Channels array like the regular
  // ImDrawListSplitter::Split method does.

  const int old_channel_capacity = splitter._Channels.Size;
  // NOTE: _Channels is not resized down, and therefore _Count <=
  // _Channels.size()!
  const int old_channel_count = splitter._Count;
  const int requested_channel_count = old_channel_count + num_channels;
  if (old_channel_capacity < old_channel_count + num_channels) {
    splitter._Channels.resize(requested_channel_count);
  }

  splitter._Count = requested_channel_count;

  for (int i = old_channel_count; i < requested_channel_count; ++i) {
    ImDrawChannel &channel = splitter._Channels[i];

    // If we're inside the old capacity region of the array, we need to
    // reuse the existing memory of the command and index buffers.
    if (i < old_channel_capacity) {
      channel._CmdBuffer.resize(0);
      channel._IdxBuffer.resize(0);
    }
    // Else, we need to construct new draw channels.
    else {
      IM_PLACEMENT_NEW(&channel) ImDrawChannel();
    }

    {
      ImDrawCmd draw_cmd;
      draw_cmd.ClipRect = draw_list->_ClipRectStack.back();
      draw_cmd.TextureId = draw_list->_TextureIdStack.back();
      channel._CmdBuffer.push_back(draw_cmd);
    }
  }
}

void ImDrawListSplitterSwapChannels(ImDrawListSplitter &splitter,
                                    const int lhs_idx, const int rhs_idx) {
  if (lhs_idx == rhs_idx) {
    return;
  }

  IM_ASSERT(lhs_idx >= 0 && lhs_idx < splitter._Count);
  IM_ASSERT(rhs_idx >= 0 && rhs_idx < splitter._Count);

  ImDrawChannel &lhs_channel = splitter._Channels[lhs_idx];
  ImDrawChannel &rhs_channel = splitter._Channels[rhs_idx];
  lhs_channel._CmdBuffer.swap(rhs_channel._CmdBuffer);
  lhs_channel._IdxBuffer.swap(rhs_channel._IdxBuffer);

  const int current_channel = splitter._Current;

  if (current_channel == lhs_idx) {
    splitter._Current = rhs_idx;
  } else if (current_channel == rhs_idx) {
    splitter._Current = lhs_idx;
  }
}

void DrawListAppendClickInteractionChannel() {
  // NOTE: don't use this function outside of EndNodeEditor. Using this
  // before all nodes have been added will screw up the node draw order.
  ImDrawListGrowChannels(GImGrid->CanvasDrawList, 1);
}

int DrawListSubmissionIdxToBackgroundChannelIdx(const int submission_idx) {
  // NOTE: the first channel is the canvas background, i.e. the grid
  return 1 + 2 * submission_idx;
}

int DrawListSubmissionIdxToForegroundChannelIdx(const int submission_idx) {
  return DrawListSubmissionIdxToBackgroundChannelIdx(submission_idx) + 1;
}

void DrawListActivateClickInteractionChannel() {
  GImGrid->CanvasDrawList->_Splitter.SetCurrentChannel(
      GImGrid->CanvasDrawList, GImGrid->CanvasDrawList->_Splitter._Count - 1);
}

void DrawListAddEntry(const int node_idx) {
  GImGrid->EntryIdxToSubmissionIdx.SetInt(
      static_cast<ImGuiID>(node_idx), GImGrid->EntryIdxSubmissionOrder.Size);
  GImGrid->EntryIdxSubmissionOrder.push_back(node_idx);
  ImDrawListGrowChannels(GImGrid->CanvasDrawList, 2);
}

void DrawListActivateCurrentEntryForeground() {
  const int foreground_channel_idx =
      DrawListSubmissionIdxToForegroundChannelIdx(
          GImGrid->EntryIdxSubmissionOrder.Size - 1);
  GImGrid->CanvasDrawList->_Splitter.SetCurrentChannel(GImGrid->CanvasDrawList,
                                                       foreground_channel_idx);
}

[[maybe_unused]] void DrawListActivateEntryBackground(const int node_idx) {
  const int submission_idx = GImGrid->EntryIdxToSubmissionIdx.GetInt(
      static_cast<ImGuiID>(node_idx), -1);
  // There is a discrepancy in the submitted node count and the rendered
  // node count! Did you call one of the following functions
  // * EditorContextMoveToNode
  // * SetNodeScreenSpacePos
  // * SetNodeGridSpacePos
  // * SetNodeDraggable
  // after the BeginNode/EndNode function calls?
  IM_ASSERT(submission_idx != -1);
  const int background_channel_idx =
      DrawListSubmissionIdxToBackgroundChannelIdx(submission_idx);
  GImGrid->CanvasDrawList->_Splitter.SetCurrentChannel(GImGrid->CanvasDrawList,
                                                       background_channel_idx);
}

void DrawListSwapSubmissionIndices(const int lhs_idx, const int rhs_idx) {
  IM_ASSERT(lhs_idx != rhs_idx);

  const int lhs_foreground_channel_idx =
      DrawListSubmissionIdxToForegroundChannelIdx(lhs_idx);
  const int lhs_background_channel_idx =
      DrawListSubmissionIdxToBackgroundChannelIdx(lhs_idx);
  const int rhs_foreground_channel_idx =
      DrawListSubmissionIdxToForegroundChannelIdx(rhs_idx);
  const int rhs_background_channel_idx =
      DrawListSubmissionIdxToBackgroundChannelIdx(rhs_idx);

  ImDrawListSplitterSwapChannels(GImGrid->CanvasDrawList->_Splitter,
                                 lhs_background_channel_idx,
                                 rhs_background_channel_idx);
  ImDrawListSplitterSwapChannels(GImGrid->CanvasDrawList->_Splitter,
                                 lhs_foreground_channel_idx,
                                 rhs_foreground_channel_idx);
}

void DrawListSortChannelsByDepth(const ImVector<int> &node_idx_depth_order) {
  if (GImGrid->EntryIdxToSubmissionIdx.Data.Size < 2) {
    return;
  }

  IM_ASSERT(node_idx_depth_order.Size == GImGrid->EntryIdxSubmissionOrder.Size);

  int start_idx = node_idx_depth_order.Size - 1;

  while (node_idx_depth_order[start_idx] ==
         GImGrid->EntryIdxSubmissionOrder[start_idx]) {
    if (--start_idx == 0) {
      // early out if submission order and depth order are the same
      return;
    }
  }

  // TODO: this is an O(N^2) algorithm. It might be worthwhile revisiting
  // this to see if the time complexity can be reduced.

  for (int depth_idx = start_idx; depth_idx > 0; --depth_idx) {
    const int node_idx = node_idx_depth_order[depth_idx];

    // Find the current index of the node_idx in the submission order array
    int submission_idx = -1;
    for (int i = 0; i < GImGrid->EntryIdxSubmissionOrder.Size; ++i) {
      if (GImGrid->EntryIdxSubmissionOrder[i] == node_idx) {
        submission_idx = i;
        break;
      }
    }
    IM_ASSERT(submission_idx >= 0);

    if (submission_idx == depth_idx) {
      continue;
    }

    for (int j = submission_idx; j < depth_idx; ++j) {
      DrawListSwapSubmissionIndices(j, j + 1);
      ImSwap(GImGrid->EntryIdxSubmissionOrder[j],
             GImGrid->EntryIdxSubmissionOrder[j + 1]);
    }
  }
}

bool MouseInCanvas() {
  // This flag should be true either when hovering or clicking something in
  // the canvas.
  const bool is_window_hovered_or_focused =
      ImGui::IsWindowHovered() || ImGui::IsWindowFocused();

  return is_window_hovered_or_focused &&
         GImGrid->CanvasRectScreenSpace.Contains(ImGui::GetMousePos());
}

void BeginCanvasInteraction() {
  const bool any_ui_element_hovered =
      GImGrid->HoveredEntryIdx.HasValue() || ImGui::IsAnyItemHovered();

  const bool mouse_not_in_canvas = !MouseInCanvas();

  if (GImGrid->ClickInteraction.Type != ImGridClickInteractionType_None ||
      any_ui_element_hovered || mouse_not_in_canvas) {
    return;
  }
}

void GridCacheRects(ImGridEngine &ctx, float w, float h, float top, float right,
                    float bottom, float left) {
  for (auto &entry : ctx.Entries) {
    entry->Rect = ImGridPosition{entry->Position.x * w + left,
                                 entry->Position.y * h + top,
                                 entry->Position.w * w - right - left,
                                 entry->Position.h * h - top - bottom};
  };
}

// Public Engine API
[[maybe_unused]] void MoveNode(ImGridContext *ctx, ImGridEntryData *entry,
              ImGridMoveOptions opts) {
  IM_ASSERT(ctx->Engine != NULL);
  ImGridEngine &engine = *ctx->Engine;

  const bool was_updating = entry->Updating;
  if (!was_updating) {
    Engine::GridCleanNodes(engine);
    Engine::GridBeginUpdate(engine, entry);
  }
  Engine::GridMoveNode(engine, entry, opts);
  UpdateContainerHeight(ctx);
  if (!was_updating) {
    Engine::GridTriggerChangeEvent(engine);
    Engine::GridEndUpdate(engine);
  }
}

ImVec2 SnapOriginToGrid(const ImVec2 &pos, float grid_spacing) {
  return pos;
  return ImVec2(std::round(pos.x / grid_spacing) * grid_spacing,
                std::round(pos.y / grid_spacing) * grid_spacing);
}

void DragOrResize(ImGridContext &ctx, ImGridEngine &engine, ImGridEntry &entry,
                  ImVec2 origin, const ImVec2 entry_rel) {
  auto position = entry.GridData.PrevPosition;
  // bool resizing;

  // float m_height = IM_ROUND(engine.LastMovingCellHeight * 0.1f);
  // float m_width = IM_ROUND(engine.LastMovingCellWidth * 0.1f);

  // auto m_left = IM_MIN(m_width, engine.Options.MarginLeft);
  // auto m_right = IM_MIN(m_width, engine.Options.MarginRight);
  // auto m_top = IM_MIN(m_height, engine.Options.MarginTop);
  // auto m_bottom = IM_MIN(m_height, engine.Options.MarginBottom);

  if (entry.Draggable) {
    entry.Origin = origin + entry_rel;

    if (ctx.Engine != NULL) {
      entry.GridData.LastUIPosition = ctx.MousePos;
      entry.GridData.Moving = true;

      // add a circle at the origin
      ctx.CanvasDrawList->AddCircleFilled(entry.Origin, 5,
                                          IM_COL32(255, 0, 0, 255));
      ctx.CanvasDrawList->AddCircleFilled(
          ScreenSpaceToGridSpace(ctx, entry.Origin), 5,
          IM_COL32(40, 255, 0, 255));
      position.x = origin.x + entry_rel.x;
      position.y = origin.y + entry_rel.y;

      int prev = engine.ExtraDragRow;
      if (Engine::GridCollide(engine, &entry.GridData, position, NULL)) {
        int row = Engine::GridGetRow(engine);
        int extra = IM_MAX(0, (position.y + entry.GridData.Position.h) - row);
        if (engine.Options.MaxRow && row + extra > engine.Options.MaxRow) {
          extra = IM_MAX(0, engine.Options.MaxRow - row);
        }
        engine.ExtraDragRow = extra;
      } else {
        engine.ExtraDragRow = 0;
      }

      if (prev != engine.ExtraDragRow) {
        UpdateContainerHeight(&ctx);
      }

      if (entry.GridData.Position.x == position.x &&
          entry.GridData.Position.y == position.y) {
        return;
      }

      ImGridMoveOptions opts{};
      opts.Position = {(origin.x + entry_rel.x) / engine.LastMovingCellWidth,
                       (origin.y + entry_rel.y) / engine.LastMovingCellHeight,
                      entry.GridData.Position.w, entry.GridData.Position.h};

      entry.GridData.LastTried = opts.Position;
      opts.Skip = NULL;
      opts.CellWidth = engine.LastMovingCellWidth;
      opts.CellHeight = engine.LastMovingCellHeight;
      if (Engine::GridEntryMoveCheck(engine, &entry.GridData, opts)) {
        GridCacheRects(engine, engine.LastMovingCellWidth,
                 engine.LastMovingCellHeight, 0, 0, 0, 0);
        entry.GridData.SkipDown = false;
        engine.ExtraDragRow = 0;
        UpdateContainerHeight(&ctx);
      }

      printf("Post Move Position %f %f %f %f\n", entry.GridData.Position.x,
             entry.GridData.Position.y, entry.GridData.Position.w,
             entry.GridData.Position.h);
    }
  }
}

void TranslateSelectedEntries(ImGridContext &ctx) {
  if (!ctx.LeftMouseDragging)
    return;

  ImVec2 origin = SnapOriginToGrid(ctx.MousePos - ctx.CanvasOriginScreenSpace -
                                       ctx.Panning + ctx.PrimaryEntryOffset,
                                   ctx.Style.GridSpacing);

  for (int i = 0; i < ctx.SelectedEntryIndices.size(); ++i) {
    const ImVec2 entry_rel = ctx.SelectedEntryOffsets[i];
    const int entry_idx = ctx.SelectedEntryIndices[i];
    ImGridEntry &entry = ctx.Entries.Pool[entry_idx];
    DragOrResize(ctx, *ctx.Engine, entry, origin, entry_rel);
  }

  GridCacheRects(*ctx.Engine, ctx.Engine->LastMovingCellWidth,
                 ctx.Engine->LastMovingCellWidth, 0, 0, 0, 0);

  // add a preview box where this will snap to if dropped
  for (int i = 0; i < ctx.SelectedEntryIndices.size(); ++i) {
    const int entry_idx = ctx.SelectedEntryIndices[i];
    ImGridEntry &entry = ctx.Entries.Pool[entry_idx];

    // have to go from grid space x, y, w, h to a rect of min and max x,y
    auto new_x = entry.GridData.Position.x * ctx.Engine->LastMovingCellWidth;
    auto new_y = entry.GridData.Position.y * ctx.Engine->LastMovingCellHeight;
       
    auto min = ScreenSpaceToGridSpace(ctx, ImVec2(new_x, new_y));
    auto max = ScreenSpaceToGridSpace(
        ctx, ImVec2(new_x + entry.GridData.Position.w * ctx.Style.GridSpacing,
                    new_y + entry.GridData.Position.h * ctx.Style.GridSpacing));
    ctx.CanvasDrawList->AddCircleFilled(min, 5, IM_COL32(0, 0, 255, 255));
    auto a = ImRect(min, max);
    printf("Preview Rect %f %f %f %f\n", a.Min.x, a.Min.y, a.Max.x, a.Max.y);
    entry.PreviewRect = a; // {new_x, new_y, width, height};

    entry.HasPreview = true;
  }
}

void OnStartMoving(ImGridEngine &engine, ImGridEntry &entry,
                   const int cell_width, const int cell_height) {
  Engine::GridCleanNodes(engine);
  Engine::GridBeginUpdate(engine, &entry.GridData);

  entry.Moving = true;
  GridCacheRects(engine, cell_width, cell_height, 0, 0, 0, 0);
}

void OnEndMoving(ImGridEngine &engine, ImGridEntry &entry) {
  entry.Moving = false;
  const bool width_changed =
      entry.GridData.Position.w != entry.GridData.PrevPosition.w;

  engine.ExtraDragRow = 0;
  UpdateContainerHeight(engine.ParentContext);

  Engine::GridTriggerChangeEvent(engine);
  Engine::GridEndUpdate(engine);

  (void)width_changed;
  // if (entry.Resizing && width_changed) {
  // }
  entry.HasPreview = false;
  entry.Origin = {entry.GridData.Position.x * engine.LastMovingCellWidth, entry.GridData.Position.y * engine.LastMovingCellHeight};
}

void ClickInteractionUpdate(ImGridContext &ctx) {
  switch (ctx.ClickInteraction.Type) {

  case ImGridClickInteractionType_Entry: {
    TranslateSelectedEntries(ctx);
    if (ctx.LeftMouseReleased) {
      ctx.ClickInteraction.Type = ImGridClickInteractionType_None;
      for (int i = 0; i < ctx.SelectedEntryIndices.size(); ++i) {
        const int entry_idx = ctx.SelectedEntryIndices[i];
        ImGridEntry &entry = ctx.Entries.Pool[entry_idx];
        OnEndMoving(*ctx.Engine, entry);
      }
    }
    break;
  }
  case ImGridClickInteractionType_ImGuiItem: {
    if (ctx.LeftMouseReleased) {
      ctx.ClickInteraction.Type = ImGridClickInteractionType_None;
    }
    break;
  }
  case ImGridClickInteractionType_Resizing: {
    if (ctx.LeftMouseReleased)
      ctx.ClickInteraction.Type = ImGridClickInteractionType_None;
    break;
  }
  case ImGridClickInteractionType_None:
    break;
  }
}

void DrawEntryDecorations(ImGridEntry &entry) {
  if (entry.Resizable) {
    const ImRect resize_grabber_rect =
        ImRect(entry.Rect.Max - ImVec2(5, 5), entry.Rect.Max);

    // HACK: this ID is wrong
    ImGui::ButtonBehavior(resize_grabber_rect, entry.Id + 3,
                          &entry.PreviewHovered, &entry.PreviewHeld);
    if (entry.PreviewHeld || entry.PreviewHovered)
      ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNWSE);
  }
}

void DrawEntryPreview(ImGridContext &ctx, const ImGridEntry &entry) {
  ctx.CanvasDrawList->AddRect(
      entry.PreviewRect.Min, entry.PreviewRect.Max,
      entry.ColorStyle.PreviewOutline, entry.LayoutStyle.CornerRounding,
      ImDrawFlags_None, entry.LayoutStyle.BorderThickness);
  ctx.CanvasDrawList->AddRectFilled(
      entry.PreviewRect.Min, entry.PreviewRect.Max,
      entry.ColorStyle.PreviewFill, entry.LayoutStyle.CornerRounding);
}

[[maybe_unused]] void DrawEntry(ImGridContext &ctx, const int entry_idx) {
  ImGridEntry &entry = ctx.Entries.Pool[entry_idx];

  // ImGui::SetCursorPos(entry.Origin + GImGrid->Panning);
  ImGui::SetCursorPos(
      ImVec2(entry.GridData.Position.x, entry.GridData.Position.y) +
      GImGrid->Panning);
  ImU32 entry_background = entry.ColorStyle.Background;
  ImU32 titlebar_background = entry.ColorStyle.Titlebar;

  const bool entry_hovered = ctx.HoveredEntryIdx == entry_idx;

  if (GImGrid->SelectedEntryIndices.contains(entry_idx)) {
    entry_background = entry.ColorStyle.BackgroundSelected;
    titlebar_background = entry.ColorStyle.TitlebarSelected;
  } else if (entry_hovered) {
    entry_background = entry.ColorStyle.BackgroundHovered;
    titlebar_background = entry.ColorStyle.TitlebarHovered;
  }

  GImGrid->CanvasDrawList->AddRectFilled(entry.Rect.Min, entry.Rect.Max,
                                         entry_background,
                                         entry.LayoutStyle.CornerRounding);

  if (entry.TitleBarContentRect.GetHeight() > 0.f) {
    ImRect title_bar_rect = GetEntryTitleRect(entry);

    GImGrid->CanvasDrawList->AddRectFilled(
        title_bar_rect.Min, title_bar_rect.Max, titlebar_background,
        entry.LayoutStyle.CornerRounding, ImDrawFlags_RoundCornersTop);
  }

  ctx.CanvasDrawList->AddRect(
      entry.Rect.Min, entry.Rect.Max, entry.ColorStyle.Outline,
      entry.LayoutStyle.CornerRounding, ImDrawFlags_RoundCornersAll,
      entry.LayoutStyle.BorderThickness);

  if (entry_hovered)
    ctx.HoveredEntryIdx = entry_idx;

  DrawEntryDecorations(entry);
}

namespace {

float CellWidth(ImGridEngine &engine) { return engine.Options.Column.Columns; }

void UpdateResizeEvent() {}

void CellHeight(ImGridEngine &engine, ImGridCellHeightOption *opt = NULL,
                bool update = true) {
  if (update && opt != NULL) {
    if (engine.IsAutoCellHeight != (opt->Mode == ImGridCellHeightMode_Auto)) {
      engine.IsAutoCellHeight = opt->Mode == ImGridCellHeightMode_Auto;
      UpdateResizeEvent();
    }
  }

  if (opt != NULL && (opt->Mode == ImGridCellHeightMode_Initial ||
                      opt->Mode == ImGridCellHeightMode_Auto)) {
    opt = NULL;
  }

  if (opt == NULL) {
    float margin_diff = -engine.Options.MarginRight -
                        engine.Options.MarginLeft + engine.Options.MarginTop +
                        engine.Options.MarginBottom;
    opt = IM_NEW(ImGridCellHeightOption)();
    opt->Mode = ImGridCellHeightMode_Auto;
    opt->HeightPixels = CellWidth(engine) + margin_diff;
  }

  if (engine.Options.CellHeight.HeightPixels == opt->HeightPixels) {
    return;
  }

  engine.Options.CellHeight = *opt;
  DoResizeToContentCheck(engine.ParentContext);
  if (update) {
    UpdateStyles(engine.ParentContext, true);
  }
}

void Column(ImGridEngine &engine, int column,
            ImGridColumnFlags flags = ImGridColumnFlags_MoveScale) {
  if (column < 1 || column == engine.Options.Column.Columns)
    return;

  int old_column = engine.Options.Column.Columns;
  engine.Options.Column.Columns = column;

  Engine::GridColumnChanged(engine, old_column, column,
                            ImGridColumnOptions{flags});
  if (engine.IsAutoCellHeight) {
    CellHeight(engine);
  }

  DoResizeToContentCheck(engine.ParentContext, true);
  engine.IgnoreLayoutsNodeChange = true;
  Engine::GridTriggerChangeEvent(engine);
  engine.IgnoreLayoutsNodeChange = false;
}

bool CheckDynamicColumn(ImGridEngine &engine) {
  auto *resp = engine.Options.ColumnOpts;
  if (resp == NULL ||
      (resp->ColumnWidth == -1 && resp->Breakpoints.size() == 0))
    return false;

  const float column = engine.Options.Column.Columns;
  float new_column = column;
  const auto w = ImGui::GetContentRegionAvail().x;

  if (resp->ColumnWidth >= 0) {
    new_column = IM_MIN(IM_ROUND(w / resp->ColumnWidth), resp->ColumnMax);
  } else {
    new_column = resp->ColumnMax;
    int i = 0;
    while (i < resp->Breakpoints.size() && w <= resp->Breakpoints[i].Width) {
      new_column = resp->Breakpoints[i++].Column
                       ? resp->Breakpoints[i].Column >= 0
                       : column;
    }
  }
  if (new_column != column) {
    ImGridColumnFlags flags = resp->Flags;
    // find the breakpoint
    for (int i = 0; i < resp->Breakpoints.size(); i++) {
      if (new_column == resp->Breakpoints[i].Column) {
        flags |= resp->Breakpoints[i].Flags;
        break;
      }
    }
    Column(engine, new_column, flags);
    return true;
  }
  return false;
}

void InitializeEngine(ImGridContext *ctx) {
  IM_ASSERT(ctx != NULL);
  IM_ASSERT(ctx->Engine == NULL);

  GImGrid->Engine = IM_NEW(ImGridEngine)();
  GImGrid->Engine->ParentContext = GImGrid;

  CheckDynamicColumn(*ctx->Engine);

  ctx->Engine->IsAutoCellHeight =
      ctx->Engine->Options.CellHeight.Mode == ImGridCellHeightMode_Auto;
  if (ctx->Engine->IsAutoCellHeight ||
      ctx->Engine->Options.CellHeight.Mode == ImGridCellHeightMode_Initial) {
    CellHeight(*ctx->Engine, NULL, false);
  }

  GImGrid->Engine->Column = GImGrid->Engine->Options.Column.Columns;

  UpdateStyles(GImGrid, false, 0);
  BatchUpdate(GImGrid, true);
  GImGrid->Engine->Loading = true;
  for (int entry_idx = 0; entry_idx < GImGrid->Entries.Pool.size();
       ++entry_idx) {
    auto &entry = GImGrid->Entries.Pool[entry_idx];
    Engine::GridPrepareEntry(*GImGrid->Engine, &entry.GridData);
  }
  GImGrid->Engine->Loading = false;
  BatchUpdate(GImGrid, false);
}
} // namespace

void BeginEntrySelection(const int entry_idx) {
  // Don't start selecting a node if we are e.g. already creating and
  // dragging a new link! New link creation can happen when the mouse is
  // clicked over a node, but within the hover radius of a pin.
  if (GImGrid->ClickInteraction.Type != ImGridClickInteractionType_None)
    return;

  // Handle resizing
  ImGridEntry &entry = GImGrid->Entries.Pool[entry_idx];
  if (entry.PreviewHeld)
    GImGrid->ClickInteraction.Type = ImGridClickInteractionType_Resizing;

  if (entry.PreviewHovered || entry.PreviewHeld)
    return;

  GImGrid->ClickInteraction.Type = ImGridClickInteractionType_Entry;
  GImGrid->Engine->LastMovingCellWidth = CellWidth(*GImGrid->Engine);
  GImGrid->Engine->LastMovingCellHeight =
      GImGrid->Engine->Options.CellHeight.HeightPixels;
  printf("Cell Width %f\n", GImGrid->Engine->LastMovingCellWidth);
  printf("Cell Height %f\n", GImGrid->Engine->LastMovingCellHeight);
  OnStartMoving(*GImGrid->Engine, entry, GImGrid->Engine->LastMovingCellWidth,
                GImGrid->Engine->LastMovingCellHeight);

  // If the node is not already contained in the selection, then we want
  // only the interaction node to be selected, effective immediately.
  //
  // If the multiple selection modifier is active, we want to add this node
  // to the current list of selected nodes.
  //
  // Otherwise, we want to allow for the possibility of multiple nodes to be
  // moved at once.
  if (!GImGrid->SelectedEntryIndices.contains(entry_idx)) {
    if (!GImGrid->MultipleSelectModifier)
      GImGrid->SelectedEntryIndices.clear();
    GImGrid->SelectedEntryIndices.push_back(entry_idx);
  }
  // Deselect a previously-selected node
  else if (GImGrid->MultipleSelectModifier) {
    const int *const node_ptr = GImGrid->SelectedEntryIndices.find(entry_idx);
    GImGrid->SelectedEntryIndices.erase(node_ptr);

    // Don't allow dragging after deselecting
    GImGrid->ClickInteraction.Type = ImGridClickInteractionType_None;
  }

  // To support snapping of multiple nodes, we need to store the offset of
  // each node in the selection to the origin of the dragged node.
  const ImVec2 ref_origin = GImGrid->Entries.Pool[entry_idx].Origin;
  GImGrid->PrimaryEntryOffset = ref_origin + GImGrid->CanvasOriginScreenSpace +
                                GImGrid->Panning - GImGrid->MousePos;

  GImGrid->SelectedEntryOffsets.clear();
  for (int idx = 0; idx < GImGrid->SelectedEntryIndices.Size; idx++) {
    const int node = GImGrid->SelectedEntryIndices[idx];
    const ImVec2 node_origin = GImGrid->Entries.Pool[node].Origin - ref_origin;
    GImGrid->SelectedEntryOffsets.push_back(node_origin);
  }
}

ImOptionalIndex ResolveHoveredEntry(const ImVector<int> &depth_stack,
                                    const ImVector<int> OverlappingIndices) {
  if (OverlappingIndices.size() == 0) {
    return ImOptionalIndex();
  }

  if (OverlappingIndices.size() == 1) {
    return ImOptionalIndex(OverlappingIndices[0]);
  }

  int largest_depth_idx = -1;
  int node_idx_on_top = -1;

  for (int i = 0; i < OverlappingIndices.size(); ++i) {
    const int node_idx = OverlappingIndices[i];
    for (int depth_idx = 0; depth_idx < depth_stack.size(); ++depth_idx) {
      if (depth_stack[depth_idx] == node_idx &&
          (depth_idx > largest_depth_idx)) {
        largest_depth_idx = depth_idx;
        node_idx_on_top = node_idx;
      }
    }
  }

  IM_ASSERT(node_idx_on_top != -1);
  return ImOptionalIndex(node_idx_on_top);
}

void DrawGrid(const ImVec2 &canvas_size) {
  const ImVec2 offset = GImGrid->Panning;
  ImU32 line_color = GImGrid->Style.Colors[ImGridCol_GridLine];
  ImU32 line_color_prim = GImGrid->Style.Colors[ImGridCol_GridLinePrimary];
  bool draw_primary = GImGrid->Style.Flags & ImGridStyleFlags_GridLinesPrimary;

  for (float x = fmodf(offset.x, GImGrid->Style.GridSpacing); x < canvas_size.x;
       x += GImGrid->Style.GridSpacing) {
    GImGrid->CanvasDrawList->AddLine(
        SpaceToScreenSpace(ImVec2(x, 0.0f)),
        SpaceToScreenSpace(ImVec2(x, canvas_size.y)),
        offset.x - x == 0.f && draw_primary ? line_color_prim : line_color);
  }

  for (float y = fmodf(offset.y, GImGrid->Style.GridSpacing); y < canvas_size.y;
       y += GImGrid->Style.GridSpacing) {
    GImGrid->CanvasDrawList->AddLine(
        SpaceToScreenSpace(ImVec2(0.0f, y)),
        SpaceToScreenSpace(ImVec2(canvas_size.x, y)),
        offset.y - y == 0.f && draw_primary ? line_color_prim : line_color);
  }

  // add any previews
  for (int entry_idx = 0; entry_idx < GImGrid->Entries.Pool.size();
       ++entry_idx) {
    const auto &entry = GImGrid->Entries.Pool[entry_idx];
    if (!entry.HasPreview)
      continue;
    DrawEntryPreview(*GImGrid, entry);
  }
}

} // namespace

} // namespace ImGrid

namespace ImGrid {

ImGridContext *CreateContext() {
  ImGridContext *ctx = IM_NEW(ImGridContext)();
  if (GImGrid == NULL)
    SetCurrentContext(ctx);
  Initialize(ctx);
  return ctx;
}

ImGridContext *GetCurrentContext() { return GImGrid; }

void SetCurrentContext(ImGridContext *ctx) { GImGrid = ctx; }

ImGridIO &GetIO() { return GImGrid->IO; }

void StyleColorsDark(ImGridStyle *dest) {
  if (dest == nullptr)
    dest = &GImGrid->Style;

  dest->Colors[ImGridCol_EntryBackground] = IM_COL32(50, 50, 50, 255);
  dest->Colors[ImGridCol_EntryBackgroundHovered] = IM_COL32(75, 75, 75, 255);
  dest->Colors[ImGridCol_EntryBackgroundSelected] = IM_COL32(75, 75, 75, 255);
  dest->Colors[ImGridCol_EntryOutline] = IM_COL32(100, 100, 100, 255);
  dest->Colors[ImGridCol_EntryPreviewFill] = IM_COL32(0, 0, 225, 100);
  dest->Colors[ImGridCol_EntryPreviewOutline] = IM_COL32(0, 0, 175, 175);
  // title bar colors match ImGui's titlebg colors
  dest->Colors[ImGridCol_TitleBar] = IM_COL32(41, 74, 122, 255);
  dest->Colors[ImGridCol_TitleBarHovered] = IM_COL32(66, 150, 250, 255);
  dest->Colors[ImGridCol_TitleBarSelected] = IM_COL32(66, 150, 250, 255);
  dest->Colors[ImGridCol_BoxSelector] = IM_COL32(61, 133, 224, 30);
  dest->Colors[ImGridCol_BoxSelectorOutline] = IM_COL32(61, 133, 224, 150);

  dest->Colors[ImGridCol_GridBackground] = IM_COL32(40, 40, 50, 200);
  dest->Colors[ImGridCol_GridLine] = IM_COL32(200, 200, 200, 40);
  dest->Colors[ImGridCol_GridLinePrimary] = IM_COL32(240, 240, 240, 60);
}

struct ImGridStyleVarInfo {
  ImGuiDataType Type;
  ImU32 Count;
  ImU32 Offset;
  void *GetVarPtr(ImGridStyle *style) const {
    return (void *)((unsigned char *)style + Offset);
  }
};

static const ImGridStyleVarInfo GStyleVarInfo[] = {
    {ImGuiDataType_Float, 1, (ImU32)offsetof(ImGridStyle, GridSpacing)},
    {ImGuiDataType_Float, 1, (ImU32)offsetof(ImGridStyle, EntryCornerRounding)},
    {ImGuiDataType_Float, 2, (ImU32)offsetof(ImGridStyle, EntryPadding)},
    {ImGuiDataType_Float, 1,
     (ImU32)offsetof(ImGridStyle, EntryBorderThickness)},
};

static const ImGridStyleVarInfo *GetStyleVarInfo(ImGridStyleVar idx) {
  IM_ASSERT(idx >= 0 && idx < ImGridStyleVar_COUNT);
  IM_ASSERT(IM_ARRAYSIZE(GStyleVarInfo) == ImGridStyleVar_COUNT);
  return &GStyleVarInfo[idx];
}

void PushStyleVar(const ImGridStyleVar item, const float value) {
  const ImGridStyleVarInfo *var_info = GetStyleVarInfo(item);
  if (var_info->Type == ImGuiDataType_Float && var_info->Count == 1) {
    float &style_var = *(float *)var_info->GetVarPtr(&GImGrid->Style);
    GImGrid->StyleModifierStack.push_back(
        ImGridStyleVarElement(item, style_var));
    style_var = value;
    return;
  }
  IM_ASSERT(0 &&
            "Called PushStyleVar() float variant but variable is not a float!");
}

void PushStyleVar(const ImGridStyleVar item, const ImVec2 &value) {
  const ImGridStyleVarInfo *var_info = GetStyleVarInfo(item);
  if (var_info->Type == ImGuiDataType_Float && var_info->Count == 2) {
    ImVec2 &style_var = *(ImVec2 *)var_info->GetVarPtr(&GImGrid->Style);
    GImGrid->StyleModifierStack.push_back(
        ImGridStyleVarElement(item, style_var));
    style_var = value;
    return;
  }
  IM_ASSERT(
      0 &&
      "Called PushStyleVar() ImVec2 variant but variable is not a ImVec2!");
}

void PopStyleVar(int count) {
  while (count > 0) {
    IM_ASSERT(GImGrid->StyleModifierStack.size() > 0);
    const ImGridStyleVarElement style_backup =
        GImGrid->StyleModifierStack.back();
    GImGrid->StyleModifierStack.pop_back();
    const ImGridStyleVarInfo *var_info = GetStyleVarInfo(style_backup.Item);
    void *style_var = var_info->GetVarPtr(&GImGrid->Style);
    if (var_info->Type == ImGuiDataType_Float && var_info->Count == 1) {
      ((float *)style_var)[0] = style_backup.FloatValue[0];
    } else if (var_info->Type == ImGuiDataType_Float && var_info->Count == 2) {
      ((float *)style_var)[0] = style_backup.FloatValue[0];
      ((float *)style_var)[1] = style_backup.FloatValue[1];
    }
    count--;
  }
}

void UpdateContainerHeight(ImGridContext *ctx) {
  IM_ASSERT(ctx != NULL);
  IM_ASSERT(ctx->Engine != NULL);
  IM_ASSERT(ctx->Engine->ParentContext != NULL);
  ImGridEngine &engine = *ctx->Engine;

  if (engine.BatchMode)
    return;
  int row = Engine::GridGetRow(engine) + engine.ExtraDragRow;

  auto cell_height = engine.Options.CellHeight.Mode == ImGridCellHeightMode_Auto
                         ? engine.Options.CellHeight.HeightPixels
                         : engine.Options.CellHeight.HeightPixels * row;

  // TODO: determine the content heights

  if (row) {
    engine.ParentContext->GridHeight = row * cell_height;
  }
}

void DoResizeToContentCheck(ImGridContext *ctx, bool delay,
                            ImGridEntryData *entry) {
  (void)entry;
  (void)delay;
  (void)ctx;
}
void PrepareElement(ImGridContext *ctx, ImGridEntryData *entry,
                    bool trigger_add_event) {
  IM_ASSERT(ctx != NULL);
  IM_ASSERT(ctx->Engine != NULL);
  IM_ASSERT(entry != NULL);
  ImGridEngine &engine = *ctx->Engine;
  auto *node = Engine::GridAddNode(engine, entry, trigger_add_event);
  (void)node;
  DoResizeToContentCheck(ctx, false, entry);
}

void MakeWidget(ImGridContext *ctx, ImGridEntryData *entry) {
  IM_ASSERT(ctx != NULL);
  IM_ASSERT(ctx->Engine != NULL);
  IM_ASSERT(entry != NULL);
  ImGridEngine &engine = *ctx->Engine;
  entry->ParentContext = ctx->Engine;
  PrepareElement(ctx, entry, true);
  UpdateContainerHeight(ctx);

  // TODO: handle sub grids

  if (engine.Options.Column.Columns == 1)
    engine.IgnoreLayoutsNodeChange = true;

  Engine::GridTriggerAddEvent(engine);
  Engine::GridTriggerChangeEvent(engine);
  engine.IgnoreLayoutsNodeChange = false;
}

void UpdateStyles(ImGridContext *ctx, bool force_update, int max_row) {
  (void)force_update;
  IM_ASSERT(ctx != NULL);
  IM_ASSERT(ctx->Engine != NULL);
  ImGridEngine &engine = *ctx->Engine;
  if (max_row < 0)
    max_row = Engine::GridGetRow(engine);

  UpdateContainerHeight(ctx);
}

void BatchUpdate(ImGridContext *ctx, bool flag) {
  IM_ASSERT(ctx != NULL);
  IM_ASSERT(ctx->Engine != NULL);
  ImGridEngine &engine = *ctx->Engine;
  Engine::GridBatchUpdate(engine, flag);
  if (!flag) {
    UpdateContainerHeight(ctx);
    Engine::GridTriggerRemoveEvent(engine);
    Engine::GridTriggerAddEvent(engine);
    Engine::GridTriggerChangeEvent(engine);
  }
}

void BeginGrid() {

  IM_ASSERT(GImGrid->CurrentScope == ImGridScope_None);
  GImGrid->CurrentScope = ImGridScope_Grid;

  // reset state
  GImGrid->GridContentBounds = ImRect(FLT_MAX, FLT_MAX, -FLT_MAX, -FLT_MAX);
  ObjectPoolReset(GImGrid->Entries);

  GImGrid->HoveredEntryIdx.Reset();
  GImGrid->HoveredEntryTitleBarIdx.Reset();
  GImGrid->EntryIndicesOverlappingWithMouse.clear();
  GImGrid->EntryTitleBarIndicesOverlappingWithMouse.clear();

  GImGrid->MousePos = ImGui::GetIO().MousePos;
  GImGrid->LeftMouseClicked = ImGui::IsMouseClicked(0);
  GImGrid->LeftMouseReleased = ImGui::IsMouseReleased(0);
  GImGrid->LeftMouseDragging = ImGui::IsMouseDragging(0, 0.0f);

  GImGrid->MultipleSelectModifier =
      (GImGrid->IO.MultipleSelectModifier.Modifier != NULL
           ? *GImGrid->IO.MultipleSelectModifier.Modifier
           : ImGui::GetIO().KeyCtrl);

  ImGui::BeginGroup();
  {
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(1.f, 1.f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.f, 0.f));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(40, 40, 50, 200));
    ImGui::BeginChild("editor_scrolling_region", ImVec2(0.f, 0.f), true,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoMove |
                          ImGuiWindowFlags_NoScrollWithMouse);
    GImGrid->CanvasOriginScreenSpace = ImGui::GetCursorScreenPos();

    // NOTE: we have to fetch the canvas draw list *after* we call
    // BeginChild(), otherwise the ImGui UI elements are going to be
    // rendered into the parent window draw list.
    DrawListSet(ImGui::GetWindowDrawList());

    {
      const ImVec2 canvas_size = ImGui::GetWindowSize();
      GImGrid->CanvasRectScreenSpace =
          ImRect(SpaceToScreenSpace(ImVec2(0.f, 0.f)),
                 SpaceToScreenSpace(canvas_size));

      DrawGrid(canvas_size);
    }
  }
}

void InsertNewEntry(ImGridContext *ctx, ImGridEntry *node, bool add_remove) {

  IM_ASSERT(ctx != NULL);
  IM_ASSERT(ctx->Engine != NULL);
  IM_ASSERT(node != NULL);
  ImGridEngine &engine = *ctx->Engine;
  ImGridEntryData *entry = &node->GridData;

  ImGridPosition copy = entry->Position;

  const int column = engine.Column;
  copy.w = copy.w == -1 ? 1 : copy.w;
  copy.h = copy.h == -1 ? 1 : copy.h;

  int max_column = copy.x == -1 ? 0 : copy.x + copy.w;
  if (max_column > column) {
    engine.IgnoreLayoutsNodeChange = true;
    ImVector<ImGridEntryData *> entries;
    entries.push_back(entry);
    Engine::GridCacheLayout(engine, entries, max_column, true);
  }

  // skipped a section here

  BatchUpdate(ctx);

  engine.Loading = true;

  ImVector<ImGridEntryData *> updates;

  if (entry->AutoSize)
    entry->Position.w =
        IM_FLOOR(node->Rect.GetWidth() / ctx->Style.GridSpacing);

  Engine::GridNodeBoundFix(engine, entry);

  if (entry->AutoPosition || entry->Position.x == -1 ||
      entry->Position.y == -1) {
    entry->Position.w =
        (entry->Position.w <= 0
             ? IM_FLOOR(node->Rect.GetWidth() / ctx->Style.GridSpacing)
             : entry->Position.w);
    entry->Position.h =
        (entry->Position.h <= 0
             ? IM_FLOOR(node->Rect.GetHeight() / ctx->Style.GridSpacing)
             : entry->Position.h);
    Engine::GridFindSpace(engine, entry, engine.Entries, engine.Column);
  }
  engine.Entries.push_back(entry);

  if (add_remove) {
    entry->AutoPosition = true;
    Engine::GridPrepareEntry(engine, entry);
    MakeWidget(ctx, entry);
  }

  engine.Loading = false;
  BatchUpdate(ctx, false);
  engine.IgnoreLayoutsNodeChange = false;
}

void EndGrid() {
  IM_ASSERT(GImGrid != NULL);
  IM_ASSERT(GImGrid->CurrentScope == ImGridScope_Grid);
  GImGrid->CurrentScope = ImGridScope_None;

  bool no_grid_content = GImGrid->GridContentBounds.IsInverted();
  if (no_grid_content)
    GImGrid->GridContentBounds =
        ScreenSpaceToGridSpace(*GImGrid, GImGrid->CanvasRectScreenSpace);

  if (GImGrid->LeftMouseClicked && ImGui::IsAnyItemActive())
    GImGrid->ClickInteraction.Type = ImGridClickInteractionType_ImGuiItem;

  if (GImGrid->ClickInteraction.Type == ImGridClickInteractionType_None &&
      MouseInCanvas()) {
    GImGrid->HoveredEntryIdx = ResolveHoveredEntry(
        GImGrid->EntryDepthOrder, GImGrid->EntryIndicesOverlappingWithMouse);
    GImGrid->HoveredEntryTitleBarIdx =
        ResolveHoveredEntry(GImGrid->EntryDepthOrder,
                            GImGrid->EntryTitleBarIndicesOverlappingWithMouse);
  }

  // GridImpl initialization
  if (GImGrid->Engine == NULL) {
    InitializeEngine(GImGrid);
  }

  for (int entry_idx = 0; entry_idx < GImGrid->Entries.Pool.size();
       ++entry_idx) {
    if (!GridContainsEntry(GImGrid,
                           &GImGrid->Entries.Pool[entry_idx].GridData)) {
      printf("Adding new entry to grid\n");
      printf("   %f %f %f %f\n", GImGrid->Entries.Pool[entry_idx].Rect.Min.x,
             GImGrid->Entries.Pool[entry_idx].Rect.Min.y,
             GImGrid->Entries.Pool[entry_idx].Rect.Max.x,
             GImGrid->Entries.Pool[entry_idx].Rect.Max.y);
      InsertNewEntry(GImGrid, &GImGrid->Entries.Pool[entry_idx]);
      GridCacheRects(*GImGrid->Engine, GImGrid->Style.GridSpacing,
                     GImGrid->Style.GridSpacing, 0, 0, 0, 0);
      GImGrid->Entries.Pool[entry_idx].Origin =
          ImVec2(GImGrid->Entries.Pool[entry_idx].GridData.Position.x,
                 GImGrid->Entries.Pool[entry_idx].GridData.Position.y);
      GImGrid->Entries.Pool[entry_idx].GridData.ParentContext = GImGrid->Engine;
    }
    if (GImGrid->Entries.InUse[entry_idx]) {
      DrawListActivateEntryBackground(entry_idx);
      DrawEntry(*GImGrid, entry_idx);
    }
  }

  GImGrid->CanvasDrawList->ChannelsSetCurrent(0);

  DrawListAppendClickInteractionChannel();
  DrawListActivateClickInteractionChannel();

  if (GImGrid->LeftMouseClicked) {
    // if (GImGrid->HoveredEntryIdx.HasValue())
    //   BeginEntrySelection(GImGrid->HoveredEntryIdx.Value());
    if (GImGrid->HoveredEntryTitleBarIdx.HasValue())
      BeginEntrySelection(GImGrid->HoveredEntryTitleBarIdx.Value());

  } else if (GImGrid->LeftMouseClicked || GImGrid->LeftMouseReleased ||
             GImGrid->AltMouseClicked || GImGrid->AltMouseScrollDelta != 0.f) {
    BeginCanvasInteraction();
  }

  ClickInteractionUpdate(*GImGrid);

  ObjectPoolUpdate(GImGrid->Entries);

  DrawListSortChannelsByDepth(GImGrid->EntryDepthOrder);

  GImGrid->CanvasDrawList->ChannelsMerge();

  // pop style
  ImGui::EndChild();      // end scrolling region
  ImGui::PopStyleColor(); // pop child window background color
  ImGui::PopStyleVar();   // pop window padding
  ImGui::PopStyleVar();   // pop frame padding
  ImGui::EndGroup();
}

void BeginEntryTitleBar() {
  IM_ASSERT(GImGrid->CurrentScope == ImGridScope_Entry);
  ImGui::BeginGroup();
}

void EndEntryTitleBar() {
  IM_ASSERT(GImGrid != NULL);
  IM_ASSERT(GImGrid->CurrentScope == ImGridScope_Entry);
  ImGui::EndGroup();

  ImGridEntry &entry = GImGrid->Entries.Pool[GImGrid->CurrentEntryIdx];
  entry.TitleBarContentRect = GetItemRect(*GImGrid, entry, false);

  ImGui::ItemAdd(GetEntryTitleRect(entry), ImGui::GetID("title_bar"));

  ImGui::SetCursorPos(GridSpaceToSpace(*GImGrid, GetEntryContentOrigin(entry)));
}

void BeginEntry(const int entry_id) {
  // Must call BeginGrid() before BeginEntry()
  IM_ASSERT(GImGrid->CurrentScope == ImGridScope_Grid);
  GImGrid->CurrentScope = ImGridScope_Entry;

  const int entry_idx = ObjectPoolFindOrCreateIndex(GImGrid->Entries, entry_id);
  GImGrid->CurrentEntryIdx = entry_idx;

  ImGridEntry &entry = GImGrid->Entries.Pool[entry_idx];
  entry.ColorStyle.Background =
      GImGrid->Style.Colors[ImGridCol_EntryBackground];
  entry.ColorStyle.BackgroundHovered =
      GImGrid->Style.Colors[ImGridCol_EntryBackgroundHovered];
  entry.ColorStyle.BackgroundSelected =
      GImGrid->Style.Colors[ImGridCol_EntryBackgroundSelected];
  entry.ColorStyle.Outline = GImGrid->Style.Colors[ImGridCol_EntryOutline];
  entry.ColorStyle.Titlebar = GImGrid->Style.Colors[ImGridCol_TitleBar];
  entry.ColorStyle.TitlebarHovered =
      GImGrid->Style.Colors[ImGridCol_TitleBarHovered];
  entry.ColorStyle.TitlebarSelected =
      GImGrid->Style.Colors[ImGridCol_TitleBarSelected];
  entry.ColorStyle.PreviewFill =
      GImGrid->Style.Colors[ImGridCol_EntryPreviewFill];
  entry.ColorStyle.PreviewOutline =
      GImGrid->Style.Colors[ImGridCol_EntryPreviewOutline];

  entry.LayoutStyle.CornerRounding = GImGrid->Style.EntryCornerRounding;
  entry.LayoutStyle.Padding = GImGrid->Style.EntryPadding;
  entry.LayoutStyle.BorderThickness = GImGrid->Style.EntryBorderThickness;

  ImGui::SetCursorPos(
      GridSpaceToSpace(*GImGrid, GetEntryTitleBarOrigin(entry)));

  DrawListAddEntry(entry_idx);
  DrawListActivateCurrentEntryForeground();

  ImGui::PushID(entry.Id);
  ImGui::BeginGroup();
}

bool GridContainsEntry(ImGridContext *ctx, ImGridEntryData *entry) {
  (void)ctx;
  IM_ASSERT(ctx != NULL);
  IM_ASSERT(entry != NULL);
  return entry->ParentContext != NULL;
}

void EndEntry() {

  IM_ASSERT(GImGrid->CurrentScope == ImGridScope_Entry);
  GImGrid->CurrentScope = ImGridScope_Grid;

  ImGui::EndGroup();
  ImGui::PopID();

  ImGridEntry &entry = GImGrid->Entries.Pool[GImGrid->CurrentEntryIdx];
  entry.Rect = GetItemRect(*GImGrid, entry);
  entry.Rect.Expand(entry.LayoutStyle.Padding);

  entry.GridData.MinW =
      (int)(entry.Rect.GetWidth() / GImGrid->Style.GridSpacing);
  entry.GridData.MinH =

      (int)(entry.Rect.GetHeight() / GImGrid->Style.GridSpacing);
  entry.GridData.MaxW = -1;
  entry.GridData.MaxH = -1;

  GImGrid->GridContentBounds.Add(entry.Origin);
  GImGrid->GridContentBounds.Add(entry.Origin + entry.Rect.GetSize());

  if (entry.Rect.Contains(GImGrid->MousePos))
    GImGrid->EntryIndicesOverlappingWithMouse.push_back(
        GImGrid->CurrentEntryIdx);

  // GetEntryTitleRect adds padding and makes it full width
  if (GetEntryTitleRect(entry).Contains(GImGrid->MousePos)) {
    GImGrid->EntryTitleBarIndicesOverlappingWithMouse.push_back(
        GImGrid->CurrentEntryIdx);
  }
}

void RenderDebug() {

  ImGui::Text("Panning: %f %f", GImGrid->Panning.x, GImGrid->Panning.y);

  ImGui::Text("Click Interaction: %d", GImGrid->ClickInteraction.Type);
  if (GImGrid->HoveredEntryIdx.HasValue())
    ImGui::Text("Hovered ID: %d", GImGrid->HoveredEntryIdx.Value());
  else
    ImGui::Text("Hovered ID: NA");

  if (GImGrid->HoveredEntryTitleBarIdx.HasValue())
    ImGui::Text("Hovered TB ID: %d", GImGrid->HoveredEntryTitleBarIdx.Value());
  else
    ImGui::Text("Hovered TB ID: NA");

  ImGui::Text("Mouse Pos: %f %f", GImGrid->MousePos.x, GImGrid->MousePos.y);

  for (int entry_idx = 0; entry_idx < GImGrid->Entries.Pool.size();
       ++entry_idx) {
    const auto &entry = GImGrid->Entries.Pool[entry_idx];
    ImGui::Text("%d: ", entry.Id);
    ImGui::Text("   %f %f", entry.Origin.x, entry.Origin.y);
    ImGui::Text("  C:  %f %f %f %f", entry.Rect.Min.x, entry.Rect.Min.y,
                entry.Rect.Max.x, entry.Rect.Max.y);
    ImGui::Text("  TB: %f %f %f %f", entry.TitleBarContentRect.Min.x,
                entry.TitleBarContentRect.Min.y,
                entry.TitleBarContentRect.Max.x,
                entry.TitleBarContentRect.Max.y);
    ImGui::Text("   Draggable: %d Resizable: %d", entry.Draggable,
                entry.Resizable);

    ImGui::Text("Engine x: %f y: %f w: %f h: %f", entry.GridData.Position.x,
                entry.GridData.Position.y, entry.GridData.Position.w,
                entry.GridData.Position.h);
    ImGui::Text("Moving: %d", entry.GridData.Moving);
  }
}

} // namespace ImGrid
