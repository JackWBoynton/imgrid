#pragma once

#include "imgui.h"
#include "imgui_internal.h"

#include "imgrid.h"

#include <map>
#include <optional>

struct ImGridEntry;
struct ImGridContext;

struct ImGridEngine;

struct ImGridEntryData {
  ImGridPosition Position;
  ImGridEntry *Parent;
  ImGridEngine *ParentContext;

  bool AutoPosition;
  float MinW, MinH;
  float MaxW, MaxH;
  bool NoResize;
  bool NoMove;
  bool Locked;

  bool AutoSize;

  bool Dirty;
  bool Updating;
  bool Moving;
  bool SkipDown;
  ImGridPosition PrevPosition;
  ImGridPosition Rect;
  ImVec2 LastUIPosition;
  ImGridPosition LastTried;
  ImGridPosition WillFitPos;

  ImGridEntryData(ImGridPosition pos, ImGridEntry *parent)
      : Position(pos), Parent(parent), ParentContext(NULL), AutoPosition(true),
        MinW(-1), MinH(-1), MaxW(-1), MaxH(-1), NoResize(false), NoMove(false),
        Locked(false), AutoSize(true), Dirty(false), Updating(false),
        Moving(false), SkipDown(false), PrevPosition(), Rect(),
        LastUIPosition(0, 0), LastTried(), WillFitPos() {}

  ImGridEntryData(ImGridEntry *parent)
      : Position(), Parent(parent), ParentContext(NULL) {}
};

typedef int ImGridCellHeightMode; // -> enum ImGridCellHeightMode_

enum ImGridCellHeightMode_ {
  ImGridCellHeightMode_Auto = 0,
  ImGridCellHeightMode_Initial = 1 << 0,
  ImGridCellHeightMode_Fixed = 1 << 1,
};

struct ImGridCellHeightOption {
  ImGridCellHeightMode Mode;
  float HeightPixels;
  float HeightThrottle; // time delay in ms when Mode=ImGridCellHeightMode_Auto
};

struct ImGridColumnOption {
  bool Auto;
  int Columns;
};

struct ImGridBreakpoint {
  int Width;
  int Column;

  ImGridColumnFlags Flags;
};

struct ImGridColumnOpts {
  int ColumnWidth;
  int ColumnMax;

  ImVector<ImGridBreakpoint> Breakpoints;

  bool BreakpointForWindow;

  ImGridColumnFlags Flags;

  ImGridColumnOpts()
      : ColumnWidth(-1), ColumnMax(12), BreakpointForWindow(false),
        Flags(ImGridColumnFlags_MoveScale) {}
};

struct ImGridOptions {
  bool AcceptWidgets;
  bool AlwaysShowResizeHandle;
  bool Animate;
  bool Auto;

  int MarginTop;
  int MarginBottom;
  int MarginLeft;
  int MarginRight;

  ImVector<ImGridEntryData *> InitialEntries;

  ImGridCellHeightOption CellHeight;
  ImGridColumnOption Column;
  ImGridColumnOpts *ColumnOpts;

  bool DisableDrag;
  bool DisableResize;

  bool Float;
  int Margin;

  int MaxRow;
  int MinRow;

  bool SizeToContent;

  ImGridOptions()
      : AcceptWidgets(true), AlwaysShowResizeHandle(false), Animate(false),
        Auto(true), CellHeight({ImGridCellHeightMode_Auto, 10, 100}),
        Column({true, 24}), DisableDrag(false), DisableResize(false),
        Float(false), Margin(10), MaxRow(-1), MinRow(0), SizeToContent(false) {}
};

struct ImGridEngine {
  ImGridOptions Options;

  int MaxRow;
  int Column;
  bool Float;
  bool PrevFloat;
  bool BatchMode;
  bool InColumnResize;
  bool HasLocked;
  bool Loading;
  int ExtraDragRow;
  bool IgnoreLayoutsNodeChange;
  bool IsAutoCellHeight;

  float LastMovingCellHeight;
  float LastMovingCellWidth;

  ImVector<ImGridEntryData *> AddedEntries;
  ImVector<ImGridEntryData *> RemovedEntries;
  ImVector<ImGridEntryData *> Entries;
  std::map<int, ImVector<ImGridEntryData>> CacheLayouts;

  ImGridContext *ParentContext;

  ImGridEngine(ImGridOptions opts = {}) {
    Column = opts.Column.Auto ? 12 : opts.Column.Columns;
    MaxRow = opts.MaxRow;
    Float = opts.Float;
    Entries = opts.InitialEntries;
    IgnoreLayoutsNodeChange = false;
    PrevFloat = Float;
    BatchMode = false;
    InColumnResize = false;
    HasLocked = false;
    Loading = false;
    ExtraDragRow = 0;
    IsAutoCellHeight = true;
    LastMovingCellHeight = 0;
    LastMovingCellWidth = 0;
  }
};

inline bool GridPositionsAreIntercepted(ImGridPosition a, ImGridPosition b) {
  return !(a.y >= b.y + b.h || a.y + a.h <= b.y || a.x + a.w <= b.x ||
           a.x >= b.x + b.w);
}

inline bool RectsAreTouching(ImGridEntryData &a, ImGridEntryData &b) {
  return GridPositionsAreIntercepted(a.Position,
                                     {b.Position.x - 0.5f, b.Position.y - 0.5f,
                                      b.Position.w + 1.f, b.Position.h + 1.f});
}

inline bool SwapEntryPositions(ImGridEntryData &a, ImGridEntryData &b) {
  if (a.Locked || b.Locked)
    return false;

  auto swapper = [&]() {
    auto x = b.Position.x;
    auto y = b.Position.y;
    b.Position.x = a.Position.x;
    b.Position.y = a.Position.y; // b -> a position
    if (a.Position.h != b.Position.h) {
      a.Position.x = x;
      a.Position.y = b.Position.y + b.Position.h; // a -> goes after b
    } else if (a.Position.w != b.Position.w) {
      a.Position.x = b.Position.x + b.Position.w;
      a.Position.y = y; // a -> goes after b
    } else {
      a.Position.x = x;
      a.Position.y = y; // a -> old b position
    }
    return true;
  };

  std::optional<bool> touching = false;
  // same size and same row or column, and touching
  if (a.Position.w == b.Position.w && a.Position.h == b.Position.h &&
      (a.Position.x == b.Position.x || a.Position.y == b.Position.y))
    if (RectsAreTouching(a, b))
      return swapper();
  if (touching.has_value() && !touching.value())
    return false; // IFF ran test and fail, bail out

  // check for taking same columns (but different height) and touching
  if (a.Position.w == b.Position.w && a.Position.x == b.Position.x &&
      (touching.value_or(false) || (RectsAreTouching(a, b)))) {
    return swapper();
  }
  if (!touching.value_or(false))
    return false;

  // check if taking same row (but different width) and touching
  if (a.Position.h == b.Position.h && a.Position.y == b.Position.y &&
      (touching || (RectsAreTouching(a, b)))) {
    return swapper();
  }
  return false;
}

namespace ImGrid::Engine {

bool GridFindEmptyPosition(ImGridEntryData &entry, int column,
                           ImVector<ImGridEntryData *> &entries,
                           ImGridEntryData *after);

// Section [Caching]
int GridFindCacheLayout(ImGridEngine &ctx, ImGridEntryData *node, int column);
void GridCacheOneLayout(ImGridEngine &ctx, ImGridEntryData *entry, int column);

void GridNodeBoundFix(ImGridEngine &ctx, ImGridEntryData *entry,
                      bool resizing = false);

ImGridEntryData *GridPrepareEntry(ImGridEngine &ctx, ImGridEntryData *entry,
                                  bool resizing = false);

// Section [Collision]
ImGridEntryData *GridCollide(ImGridEngine &ctx, ImGridEntryData *skip,
                             ImGridPosition area, ImGridEntryData *skip2);
ImVector<ImGridEntryData *> GridCollideAll(ImGridEngine &ctx,
                                           ImGridEntryData *skip,
                                           ImGridPosition area,
                                           ImGridEntryData *skip2);

// Section [Sorting]
void GridSortNodesInplace(ImVector<ImGridEntryData *> &nodes, bool upwards);
ImVector<ImGridEntryData *> GridSortNodes(ImVector<ImGridEntryData *> nodes,
                                          bool upwards);

void GridPackEntries(ImGridEngine &ctx);

ImGridEntryData *GridCopyPosition(ImGridEntryData *a, ImGridEntryData *b,
                                  bool include_minmax = false);
ImGridEntryData *GridCopyPositionFromOpts(ImGridEntryData *a,
                                          ImGridMoveOptions *b,
                                          bool include_minmax = false);
ImGridEntryData *GridCopyPositionToOpts(ImGridEntryData *b,
                                        ImGridMoveOptions *a,
                                        bool include_minmax = false);

ImGridEntryData *
GridDirectionCollideCoverage(ImGridEntryData *entry, ImGridMoveOptions &opts,
                             ImVector<ImGridEntryData *> &collides);

bool GridUseEntireRowArea(ImGridEngine &ctx, ImGridEntryData *entry,
                          ImGridPosition new_position);

bool GridFixCollisions(ImGridEngine &ctx, ImGridEntryData *entry,
                       ImGridPosition new_position, // = entry->Position,
                       ImGridEntryData *collide = NULL,
                       ImGridMoveOptions opts = {});

bool GridMoveNode(ImGridEngine &ctx, ImGridEntryData *entry,
                  ImGridMoveOptions &opts);

ImGridEntryData *GridAddNode(ImGridEngine &ctx, ImGridEntryData *entry,
                             bool trigger_add_event = false,
                             ImGridEntryData *after = NULL);

void GridRemoveEntry(ImGridEngine &ctx, ImGridEntryData *entry,
                     bool trigger_event = false);

bool GridChangedPosConstrain(ImGridEntryData *entry, ImGridPosition &p);

int GridGetRow(ImGridEngine &ctx);

bool GridEntryMoveCheck(ImGridEngine &ctx, ImGridEntryData *entry,
                        ImGridMoveOptions opts);

void GridCleanNodes(ImGridEngine &ctx);

void GridSaveInitial(ImGridEngine &ctx);

void GridBatchUpdate(ImGridEngine &ctx, bool flag = true, bool do_pack = true);

void GridCacheLayout(ImGridEngine &ctx, ImVector<ImGridEntryData *> nodes,
                     int column, bool clear = false);

void GridCompact(ImGridEngine &ctx,
                 ImGridColumnFlags opts = ImGridColumnFlags_Compact,
                 bool do_sort = true);

void GridColumnChanged(ImGridEngine &ctx, int previous_column, int column,
                       ImGridColumnOptions opts = ImGridColumnOptions{
                           ImGridColumnFlags_MoveScale});

ImVector<ImGridEntryData *> GridGetDirtyNodes(ImGridEngine &ctx);

void GridLayoutsNodesChanged(ImGridEngine &ctx,
                             ImVector<ImGridEntryData *> &nodes);

void GridTriggerChangeEvent(ImGridEngine &ctx);
void GridTriggerAddEvent(ImGridEngine &ctx);
void GridTriggerRemoveEvent(ImGridEngine &ctx);

void GridBeginUpdate(ImGridEngine &ctx, ImGridEntryData *node);
void GridEndUpdate(ImGridEngine &ctx);

void GridFindSpace(ImGridEngine &ctx, ImGridEntryData *entry,
                   ImVector<ImGridEntryData *> &node_list, int column,
                   ImGridEntryData *after = NULL);

} // namespace ImGrid::Engine
