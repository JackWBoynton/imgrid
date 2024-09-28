
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

ImGridIO::ImGridIO()
    : AltMouseButton(ImGuiMouseButton_Middle), AutoPanningSpeed(1000.0f) {}

ImGridIO::MultipleSelectModifier::MultipleSelectModifier() : Modifier(NULL) {}

ImGridEntry::ImGridEntry(const int id, ImGridPosition pos)
    : Id(id), Position(pos), ParentContext(NULL), AutoPosition(true), MinW(-1),
      MinH(-1), MaxW(-1), MaxH(-1), NoResize(false), NoMove(false),
      Locked(false), AutoSize(true), Dirty(false), Updating(false),
      SkipDown(false), PrevPosition(), Rect(), LastUIPosition(), LastTried(),
      WillFitPos(), MovingPosition(), Moving(false), PreviewPosition(),
      HasPreview(false), BorderHovered(false), BorderHeld(false), ColorStyle(),
      LayoutStyle() {}

ImGridEntry::ImGridEntry(const int id)
    : Id(id), Position({}), ParentContext(NULL), AutoPosition(true), MinW(-1),
      MinH(-1), MaxW(-1), MaxH(-1), NoResize(false), NoMove(false),
      Locked(false), AutoSize(true), Dirty(false), Updating(false),
      SkipDown(false), PrevPosition(), Rect(), LastUIPosition(), LastTried(),
      WillFitPos(), MovingPosition(), Moving(false), PreviewPosition(),
      HasPreview(false), BorderHovered(false), BorderHeld(false), ColorStyle(),
      LayoutStyle() {}

ImGridEntry::ImGridEntry(ImGridPosition pos)
    : Id(-1), Position(pos), ParentContext(NULL), AutoPosition(true), MinW(-1),
      MinH(-1), MaxW(-1), MaxH(-1), NoResize(false), NoMove(false),
      Locked(false), AutoSize(true), Dirty(false), Updating(false),
      SkipDown(false), PrevPosition(), Rect(), LastUIPosition(), LastTried(),
      WillFitPos(), MovingPosition(), Moving(false), PreviewPosition(),
      HasPreview(false), BorderHovered(false), BorderHeld(false), ColorStyle(),
      LayoutStyle() {}

ImGridStyle::ImGridStyle()
    : GridSpacing(50.f), EntryCornerRounding(4.f), EntryPadding(8.f, 8.f),
      EntryBorderThickness(1.f), Flags(ImGridStyleFlags_None), Colors() {}

namespace ImGrid {

namespace {

void Initialize(ImGridContext *ctx) {
  ctx->HoveredEntryIdx = -1;
  ctx->HoveredEntryTitleBarIdx = -1;
  ctx->CurrentScope = ImGridScope_None;
  ctx->Zoom = 1.0f;

  StyleColorsDark();
}

inline ImRect GetItemRectInternal() {
  return ImRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
}

inline ScreenSpacePosition CanvasSpaceToScreenSpace(const ImGridContext &ctx,
                                                    const ImVec2 &v) {
  return ScreenSpacePosition{ctx.CanvasOriginScreenSpace + (v)*ctx.Zoom};
}

[[maybe_unused]] inline ImRect GetItemRect() {
  // Retrieve the current item rectangle and its size
  ImRect rect = GetItemRectInternal();
  return rect;
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
         GImGrid->CanvasRectScreenSpace.Contains(GImGrid->MousePos);
}

void BeginCanvasInteraction() {
  const bool any_ui_element_hovered =
      GImGrid->HoveredEntryIdx.HasValue() || ImGui::IsAnyItemHovered();

  const bool mouse_not_in_canvas = !MouseInCanvas();

  if (GImGrid->ClickInteraction.Type != ImGridClickInteractionType_None ||
      any_ui_element_hovered || mouse_not_in_canvas) {
    return;
  }

  const bool started_panning = GImGrid->AltMouseClicked;

  if (started_panning) {
    GImGrid->ClickInteraction.Type = ImGridClickInteractionType_Panning;
  } else if (GImGrid->LeftMouseClicked) {
    GImGrid->ClickInteraction.Type = ImGridClickInteractionType_BoxSelection;
    GImGrid->ClickInteraction.BoxSelector.Rect.Min = GImGrid->MousePos;
  }

  if (GImGrid->CtrlKeyHeld && GImGrid->MouseWheelDelta != 0.0f) {
    float zoom_increment = 0.1f;
    float new_zoom = GImGrid->Zoom + GImGrid->MouseWheelDelta * zoom_increment;
    GImGrid->Zoom =
        ImClamp(new_zoom, 0.1f, 10.0f); // Clamp zoom level between 0.1x and 10x
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
[[maybe_unused]] void MoveNode(ImGridContext *ctx, ImGridEntry *entry,
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

void DragOrResize(ImGridContext &ctx, ImGridEngine &engine, ImGridEntry &entry,
                  ImVec2 origin, const ImVec2 entry_rel) {
  auto position = entry.PrevPosition;
  // bool resizing;

  // float m_height = IM_ROUND(engine.LastMovingCellHeight * 0.1f);
  // float m_width = IM_ROUND(engine.LastMovingCellWidth * 0.1f);

  // auto m_left = IM_MIN(m_width, engine.Options.MarginLeft);
  // auto m_right = IM_MIN(m_width, engine.Options.MarginRight);
  // auto m_top = IM_MIN(m_height, engine.Options.MarginTop);
  // auto m_bottom = IM_MIN(m_height, engine.Options.MarginBottom);

  entry.MovingPosition = origin + entry_rel;

  if (ctx.Engine != NULL) {
    entry.LastUIPosition = ctx.MousePos;
    entry.Moving = true;

    position.x = origin.x + entry_rel.x;
    position.y = origin.y + entry_rel.y;

    int prev = engine.ExtraDragRow;
    if (Engine::GridCollide(engine, &entry, position, NULL)) {
      int row = Engine::GridGetRow(engine);
      int extra = IM_MAX(0, (position.y + entry.Position.h) - row);
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

    if (entry.Position.x == position.x && entry.Position.y == position.y) {
      return;
    }

    ImGridMoveOptions opts{};
    // TODO: I think this would feel more natural if rather than using the
    // center of the object to ask for the next position, we used the mouse
    // position.
    opts.Position = {
        std::ceil((origin.x + entry_rel.x) / ctx.Style.GridSpacing),
        std::ceil((origin.y + entry_rel.y) / ctx.Style.GridSpacing),
        entry.Position.w, entry.Position.h};

    entry.LastTried = opts.Position;
    opts.Skip = NULL;
    opts.CellWidth = engine.ParentContext->Style.GridSpacing;
    opts.CellHeight = engine.ParentContext->Style.GridSpacing;
    if (Engine::GridEntryMoveCheck(engine, &entry, opts)) {
      GridCacheRects(engine, engine.ParentContext->Style.GridSpacing,
                     engine.ParentContext->Style.GridSpacing, 0, 0, 0, 0);
      entry.SkipDown = false;
      engine.ExtraDragRow = 0;
      UpdateContainerHeight(&ctx);
    }
  }
}

void TranslateSelectedEntries(ImGridContext &ctx) {
  if (!ctx.LeftMouseDragging)
    return;

  // Convert mouse position to grid units
  auto origin = ctx.MousePos - ctx.CanvasOriginScreenSpace - ctx.Panning;

  for (int i = 0; i < ctx.SelectedEntryIndices.size(); ++i) {
    const ImVec2 entry_rel = ctx.SelectedEntryOffsets[i];
    const int entry_idx = ctx.SelectedEntryIndices[i];
    ImGridEntry &entry = ctx.Entries.Pool[entry_idx];
    DragOrResize(ctx, *ctx.Engine, entry, origin, entry_rel);
  }

  GridCacheRects(*ctx.Engine, 50, 50, 0, 0, 0, 0);

  // add a preview box where this will snap to if dropped
  for (int i = 0; i < ctx.SelectedEntryIndices.size(); ++i) {
    const int entry_idx = ctx.SelectedEntryIndices[i];
    ImGridEntry &entry = ctx.Entries.Pool[entry_idx];

    // have to go from grid space x, y, w, h to a rect of min and max x,y
    auto a = GetNodePreviewScreenRect(ctx, entry);
    entry.PreviewPosition = a.Min;
    entry.HasPreview = true;
  }
}

void OnStartMoving(ImGridEngine &engine, ImGridEntry &entry,
                   const int cell_width, const int cell_height) {
  Engine::GridCleanNodes(engine);
  Engine::GridBeginUpdate(engine, &entry);

  entry.Moving = true;
  GridCacheRects(engine, cell_width, cell_height, 0, 0, 0, 0);
}

void OnEndMoving(ImGridEngine &engine, ImGridEntry &entry) {
  entry.Moving = false;
  const bool width_changed = entry.Position.w != entry.PrevPosition.w;

  engine.ExtraDragRow = 0;
  UpdateContainerHeight(engine.ParentContext);

  Engine::GridTriggerChangeEvent(engine);
  Engine::GridEndUpdate(engine);

  (void)width_changed;
  entry.HasPreview = false;
}

void BoxSelectorUpdateSelection(ImGridContext &ctx, ScreenSpaceRect box_rect) {
  if (box_rect.Min.x > box_rect.Max.x) {
    ImSwap(box_rect.Min.x, box_rect.Max.x);
  }

  if (box_rect.Min.y > box_rect.Max.y) {
    ImSwap(box_rect.Min.y, box_rect.Max.y);
  }

  ctx.SelectedEntryIndices.clear();

  // Test for overlap against node rectangles

  for (int node_idx = 0; node_idx < ctx.Entries.Pool.size(); ++node_idx) {
    if (ctx.Entries.InUse[node_idx]) {
      auto &node = ctx.Entries.Pool[node_idx];
      if (box_rect.Overlaps(GetNodeScreenRect(ctx, node))) {
        ctx.SelectedEntryIndices.push_back(node_idx);
      }
    }
  }
}

void ClickInteractionUpdate(ImGridContext &ctx) {
  switch (ctx.ClickInteraction.Type) {

  case ImGridClickInteractionType_BoxSelection: {

    // update the current rect
    ctx.ClickInteraction.BoxSelector.Rect.Max = ctx.MousePos;
    auto box_rect = ctx.ClickInteraction.BoxSelector.Rect;
    BoxSelectorUpdateSelection(ctx, box_rect);

    const ImU32 box_selector_color = ctx.Style.Colors[ImGridCol_BoxSelector];
    const ImU32 box_selector_outline =
        ctx.Style.Colors[ImGridCol_BoxSelectorOutline];
    ctx.CanvasDrawList->AddRectFilled(box_rect.Min, box_rect.Max,
                                      box_selector_color);
    ctx.CanvasDrawList->AddRect(box_rect.Min, box_rect.Max,
                                box_selector_outline);

    // handle release
    if (ctx.LeftMouseReleased) {
      ImVector<int> &depth_stack = ctx.EntryDepthOrder;
      const ImVector<int> &selected_idxs = ctx.SelectedEntryIndices;

      // Bump the selected node indices, in order, to the top of the depth
      // stack. NOTE: this algorithm has worst case time complexity of O(N^2),
      // if the node selection is ~ N (due to selected_idxs.contains()).

      if ((selected_idxs.Size > 0) && (selected_idxs.Size < depth_stack.Size)) {
        int num_moved =
            0; // The number of indices moved. Stop after selected_idxs.Size
        for (int i = 0; i < depth_stack.Size - selected_idxs.Size; ++i) {
          for (int node_idx = depth_stack[i]; selected_idxs.contains(node_idx);
               node_idx = depth_stack[i]) {
            depth_stack.erase(depth_stack.begin() + static_cast<size_t>(i));
            depth_stack.push_back(node_idx);
            ++num_moved;
          }

          if (num_moved == selected_idxs.Size) {
            break;
          }
        }
      }

      ctx.ClickInteraction.Type = ImGridClickInteractionType_None;
    }

    break;
  }

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
  case ImGridClickInteractionType_Panning: {
    const bool dragging = ctx.AltMouseDragging;

    if (dragging) {
      ctx.Panning += ScreenSpacePosition(ImGui::GetIO().MouseDelta);
    } else {
      ctx.ClickInteraction.Type = ImGridClickInteractionType_None;
    }
    break;
  }
  case ImGridClickInteractionType_None:
    break;
  }
}

void DrawEntryDecorations(ImGridEntry &entry) {
  const auto entry_screen_rect = GetNodeScreenRect(*GImGrid, entry);
  if (entry.Resizable) {
    const ImRect resize_grabber_rect =
        ImRect(entry_screen_rect.Max - ImVec2(5, 5), entry_screen_rect.Max);

    // HACK: this ID is wrong
    ImGui::ButtonBehavior(resize_grabber_rect, entry.Id + 3,
                          &entry.BorderHovered, &entry.BorderHeld);
    if (entry.BorderHovered || entry.BorderHeld)
      ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNWSE);
  }
}

void DrawEntryPreview(ImGridContext &ctx, const ImGridEntry &entry) {
  const auto screen_rect = GetNodeScreenRect(ctx, entry);
  const auto preview_rect = ScreenSpaceRect(
      entry.PreviewPosition,
      entry.PreviewPosition +
          ScreenSpacePosition(screen_rect.GetWidth(), screen_rect.GetHeight()));
  ctx.CanvasDrawList->AddRect(
      preview_rect.Min, preview_rect.Max, entry.ColorStyle.PreviewOutline,
      entry.LayoutStyle.CornerRounding, ImDrawFlags_None,
      entry.LayoutStyle.BorderThickness);
  ctx.CanvasDrawList->AddRectFilled(preview_rect.Min, preview_rect.Max,
                                    entry.ColorStyle.PreviewFill,
                                    entry.LayoutStyle.CornerRounding);
}

void DrawEntry(ImGridContext &ctx, const int entry_idx) {
  ImGridEntry &entry = ctx.Entries.Pool[entry_idx];

  ImU32 entry_background = entry.ColorStyle.Background;
  // ImU32 titlebar_background = entry.ColorStyle.Titlebar;

  const bool entry_hovered = ctx.HoveredEntryIdx == entry_idx;

  if (ctx.SelectedEntryIndices.contains(entry_idx)) {
    entry_background = entry.ColorStyle.BackgroundSelected;
    // titlebar_background = entry.ColorStyle.TitlebarSelected;
  } else if (entry_hovered) {
    entry_background = entry.ColorStyle.BackgroundHovered;
    // titlebar_background = entry.ColorStyle.TitlebarHovered;
  }

  // Adjust rectangle for zoom
  auto entry_rect = GetNodeScreenRect(ctx, entry);

  ctx.CanvasDrawList->AddRectFilled(
      entry_rect.Min, entry_rect.Max, entry_background,
      entry.LayoutStyle.CornerRounding * ctx.Zoom);

  ctx.CanvasDrawList->AddRect(
      entry_rect.Min, entry_rect.Max, entry.ColorStyle.Outline,
      entry.LayoutStyle.CornerRounding * ctx.Zoom, ImDrawFlags_RoundCornersAll,
      entry.LayoutStyle.BorderThickness * ctx.Zoom);

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
    Engine::GridPrepareEntry(*GImGrid->Engine, &entry);
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
  if (entry.BorderHeld)
    GImGrid->ClickInteraction.Type = ImGridClickInteractionType_Resizing;

  if (entry.BorderHovered || entry.BorderHeld)
    return;

  GImGrid->ClickInteraction.Type = ImGridClickInteractionType_Entry;
  GImGrid->Engine->LastMovingCellWidth = GImGrid->Style.GridSpacing;
  GImGrid->Engine->LastMovingCellHeight =
      GImGrid->Engine->Options.CellHeight.HeightPixels;
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
  const auto entry_screen_rect = GetNodeScreenRect(*GImGrid, entry);
  const auto entry_position =
      entry_screen_rect.Min; // Entry's position on screen

  // Store the offset between the mouse position and the entry's position
  GImGrid->PrimaryEntryOffset = GImGrid->MousePos - entry_position;

  // entry.MoveMouseOffsetRel = entry_position;

  // For multiple selected entries, store the offset
  // relative to the primary entry
  GImGrid->SelectedEntryOffsets.clear();
  for (int idx = 0; idx < GImGrid->SelectedEntryIndices.Size; idx++) {
    const int node = GImGrid->SelectedEntryIndices[idx];
    const auto node_screen_rect =
        GetNodeScreenRect(*GImGrid, GImGrid->Entries.Pool[node]);
    const auto node_position = node_screen_rect.Min;
    const auto offset = node_position - entry_position;
    GImGrid->SelectedEntryOffsets.push_back(offset);
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

void DrawGrid(ImGridContext &ctx, const ImVec2 &canvas_size) {
  const ImVec2 offset = ctx.Panning;
  ImU32 line_color = ctx.Style.Colors[ImGridCol_GridLine];
  ImU32 line_color_prim = ctx.Style.Colors[ImGridCol_GridLinePrimary];
  bool draw_primary = ctx.Style.Flags & ImGridStyleFlags_GridLinesPrimary;

  for (float x = fmodf(offset.x, ctx.Style.GridSpacing); x < canvas_size.x;
       x += ctx.Style.GridSpacing) {
    ctx.CanvasDrawList->AddLine(
        CanvasSpaceToScreenSpace(ctx, ImVec2(x, 0.0f)),
        CanvasSpaceToScreenSpace(ctx, ImVec2(x, canvas_size.y)),
        offset.x - x == 0.f && draw_primary ? line_color_prim : line_color);
  }

  for (float y = fmodf(offset.y, ctx.Style.GridSpacing); y < canvas_size.y;
       y += ctx.Style.GridSpacing) {
    ctx.CanvasDrawList->AddLine(
        CanvasSpaceToScreenSpace(ctx, ImVec2(0.0f, y)),
        CanvasSpaceToScreenSpace(ctx, ImVec2(canvas_size.x, y)),
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
                            ImGridEntry *entry) {
  (void)entry;
  (void)delay;
  (void)ctx;
}
void PrepareElement(ImGridContext *ctx, ImGridEntry *entry,
                    bool trigger_add_event) {
  IM_ASSERT(ctx != NULL);
  IM_ASSERT(ctx->Engine != NULL);
  IM_ASSERT(entry != NULL);
  ImGridEngine &engine = *ctx->Engine;
  auto *node = Engine::GridAddNode(engine, entry, trigger_add_event);
  (void)node;
  DoResizeToContentCheck(ctx, false, entry);
}

void MakeWidget(ImGridContext *ctx, ImGridEntry *entry) {
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
  GImGrid->GridContentBounds =
      ScreenSpaceRect(FLT_MAX, FLT_MAX, -FLT_MAX, -FLT_MAX);
  ObjectPoolReset(GImGrid->Entries);

  GImGrid->HoveredEntryIdx.Reset();
  GImGrid->AutoPanningDelta = ImVec2(0, 0);
  GImGrid->HoveredEntryTitleBarIdx.Reset();
  GImGrid->EntryIndicesOverlappingWithMouse.clear();
  GImGrid->EntryTitleBarIndicesOverlappingWithMouse.clear();

  GImGrid->MousePos = ScreenSpacePosition(ImGui::GetIO().MousePos);
  GImGrid->LeftMouseClicked = ImGui::IsMouseClicked(0);
  GImGrid->LeftMouseReleased = ImGui::IsMouseReleased(0);
  GImGrid->LeftMouseDragging = ImGui::IsMouseDragging(0, 0.0f);

  GImGrid->AltMouseClicked = ImGui::IsMouseClicked(GImGrid->IO.AltMouseButton);
  GImGrid->AltMouseDragging =
      ImGui::IsMouseDragging(GImGrid->IO.AltMouseButton, 0.0f);
  GImGrid->AltMouseScrollDelta = ImGui::GetIO().MouseWheel;
  GImGrid->MouseWheelDelta = ImGui::GetIO().MouseWheel;
  GImGrid->CtrlKeyHeld = ImGui::GetIO().KeyCtrl;
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
    GImGrid->CanvasOriginScreenSpace =
        ScreenSpacePosition(ImGui::GetCursorScreenPos());

    // NOTE: we have to fetch the canvas draw list *after* we call
    // BeginChild(), otherwise the ImGui UI elements are going to be
    // rendered into the parent window draw list.
    DrawListSet(ImGui::GetWindowDrawList());

    {
      const ImVec2 canvas_size = ImGui::GetWindowSize();
      GImGrid->CanvasRectScreenSpace =
          ScreenSpaceRect(CanvasSpaceToScreenSpace(*GImGrid, ImVec2(0.f, 0.f)),
                          CanvasSpaceToScreenSpace(*GImGrid, canvas_size));

      DrawGrid(*GImGrid, canvas_size);
    }
  }
}

void InsertNewEntry(ImGridContext *ctx, ImGridEntry *node, bool add_remove) {

  IM_ASSERT(ctx != NULL);
  IM_ASSERT(ctx->Engine != NULL);
  IM_ASSERT(node != NULL);
  ImGridEngine &engine = *ctx->Engine;

  ImGridPosition copy = node->Position;

  const int column = engine.Column;
  copy.w = copy.w == -1 ? 1 : copy.w;
  copy.h = copy.h == -1 ? 1 : copy.h;

  int max_column = copy.x == -1 ? 0 : copy.x + copy.w;
  if (max_column > column) {
    engine.IgnoreLayoutsNodeChange = true;
    ImVector<ImGridEntry *> entries;
    entries.push_back(node);
    Engine::GridCacheLayout(engine, entries, max_column, true);
  }

  // skipped a section here

  BatchUpdate(ctx);

  engine.Loading = true;

  ImVector<ImGridEntry *> updates;

  Engine::GridNodeBoundFix(engine, node);

  if (node->AutoPosition || node->Position.x == -1 || node->Position.y == -1) {
    Engine::GridFindSpace(engine, node, engine.Entries, engine.Column);
  }
  engine.Entries.push_back(node);

  if (add_remove) {
    node->AutoPosition = true;
    Engine::GridPrepareEntry(engine, node);
    MakeWidget(ctx, node);
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
    GImGrid->GridContentBounds = GImGrid->CanvasRectScreenSpace;

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

  if (GImGrid->Engine == NULL) {
    InitializeEngine(GImGrid);
  }

  for (int entry_idx = 0; entry_idx < GImGrid->Entries.Pool.size();
       ++entry_idx) {
    ImGridEntry &entry = GImGrid->Entries.Pool[entry_idx];
    if (!GridContainsEntry(GImGrid, &entry)) {
      InsertNewEntry(GImGrid, &entry);
      GridCacheRects(*GImGrid->Engine, GImGrid->Style.GridSpacing,
                     GImGrid->Style.GridSpacing, 0, 0, 0, 0);
      entry.ParentContext = GImGrid->Engine;
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
    if (GImGrid->HoveredEntryTitleBarIdx.HasValue())
      BeginEntrySelection(GImGrid->HoveredEntryTitleBarIdx.Value());
  }
  if (GImGrid->LeftMouseClicked || GImGrid->LeftMouseReleased ||
      GImGrid->AltMouseClicked || GImGrid->AltMouseScrollDelta != 0.f) {
    BeginCanvasInteraction();
  }

  bool should_auto_pan =
      GImGrid->ClickInteraction.Type ==
          ImGridClickInteractionType_BoxSelection ||
      GImGrid->ClickInteraction.Type == ImGridClickInteractionType_Entry;
  if (should_auto_pan && !MouseInCanvas()) {
    auto mouse = GImGrid->MousePos;
    auto center = GImGrid->CanvasRectScreenSpace.GetCenter();
    auto direction = (center - mouse);
    direction = direction * ImInvLength(direction, 0.0);

    GImGrid->AutoPanningDelta =
        direction * ImGui::GetIO().DeltaTime * GImGrid->IO.AutoPanningSpeed;
    GImGrid->Panning += GImGrid->AutoPanningDelta;
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

  // ImGridEntry &entry = GImGrid->Entries.Pool[GImGrid->CurrentEntryIdx];

  // ImGui::ItemAdd(GetEntryTitleRect(entry), ImGui::GetID("title_bar"));

  // ImGui::SetCursorPos(GetNodeScreenRect(*GImGrid, entry).Min);
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

  ImGui::SetCursorPos(GetNodeScreenRect(*GImGrid, entry).Min);

  DrawListAddEntry(entry_idx);
  DrawListActivateCurrentEntryForeground();

  ImGui::PushID(entry.Id);
  ImGui::BeginGroup();
}

bool GridContainsEntry(ImGridContext *ctx, ImGridEntry *entry) {
  (void)ctx;
  IM_ASSERT(ctx != NULL);
  IM_ASSERT(entry != NULL);
  return entry->ParentContext != NULL;
}

void EndEntry() {

  IM_ASSERT(GImGrid->CurrentScope == ImGridScope_Entry);
  GImGrid->CurrentScope = ImGridScope_Grid;

  // Hack to force the size to be multiples of grid size
  ImGridEntry &entry = GImGrid->Entries.Pool[GImGrid->CurrentEntryIdx];

  ImGui::EndGroup();
  ImGui::PopID();

  auto entry_rect = GetItemRect();
  // add grid width/height to the entry
  UpdateNodeGridSpaceSize(*GImGrid, entry, entry_rect.GetWidth(),
                          entry_rect.GetHeight());

  // get the screen coordinates of the entry
  auto screen_rect = GetNodeScreenRect(*GImGrid, entry);

  GImGrid->GridContentBounds.Add(screen_rect.GetCenter());
  GImGrid->GridContentBounds.Add(screen_rect.Min);

  if (screen_rect.Contains(GImGrid->MousePos))
    GImGrid->EntryIndicesOverlappingWithMouse.push_back(
        GImGrid->CurrentEntryIdx);

  // GetEntryTitleRect adds padding and makes it full width
  if (screen_rect.Contains(GImGrid->MousePos)) {
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
  ImGui::Text("Panning: %f %f", GImGrid->Panning.x, GImGrid->Panning.y);

  for (int entry_idx = 0; entry_idx < GImGrid->Entries.Pool.size();
       ++entry_idx) {
    const auto &entry = GImGrid->Entries.Pool[entry_idx];
    ImGui::Text("%d: ", entry.Id);

    ImGui::Text("Engine x: %f y: %f w: %f h: %f", entry.Position.x,
                entry.Position.y, entry.Position.w, entry.Position.h);
    ImGui::Text("Moving: %d", entry.Moving);
  }
}

} // namespace ImGrid
