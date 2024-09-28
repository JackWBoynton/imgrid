#pragma once

#include "imgui.h"
#include "imgui_internal.h"

#include "imgrid.h"

#include <map>
#include <optional>

struct ImGridEntry;
struct ImGridContext;

struct ImGridEngine;

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

  ImVector<ImGridEntry *> InitialEntries;

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
        Auto(true), CellHeight({ImGridCellHeightMode_Auto, 50, 100}),
        Column({true, 1024}), DisableDrag(false), DisableResize(false),
        Float(false), Margin(10), MaxRow(-1), MinRow(0), SizeToContent(true) {}
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

  ImVector<ImGridEntry *> AddedEntries;
  ImVector<ImGridEntry *> RemovedEntries;
  ImVector<ImGridEntry *> Entries;
  std::map<int, ImVector<ImGridEntry>> CacheLayouts;

  ImGridContext *ParentContext;

  ImGridEngine(ImGridOptions opts = {}) {
    Column = opts.Column.Auto ? 1024 : opts.Column.Columns;
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

namespace ImGrid::Engine {

bool GridFindEmptyPosition(ImGridEngine &ctx, ImGridEntry &entry, int column,
                           ImVector<ImGridEntry *> &entries,
                           ImGridEntry *after);

// Section [Caching]
int GridFindCacheLayout(ImGridEngine &ctx, ImGridEntry *node, int column);
void GridCacheOneLayout(ImGridEngine &ctx, ImGridEntry *entry, int column);

void GridNodeBoundFix(ImGridEngine &ctx, ImGridEntry *entry,
                      bool resizing = false);

ImGridEntry *GridPrepareEntry(ImGridEngine &ctx, ImGridEntry *entry,
                              bool resizing = false);

// Section [Collision]
ImGridEntry *GridCollide(ImGridEngine &ctx, ImGridEntry *skip,
                         ImGridPosition area, ImGridEntry *skip2);
ImVector<ImGridEntry *> GridCollideAll(ImGridEngine &ctx, ImGridEntry *skip,
                                       ImGridPosition area, ImGridEntry *skip2);

// Section [Sorting]
void GridSortNodesInplace(ImVector<ImGridEntry *> &nodes, bool upwards);
ImVector<ImGridEntry *> GridSortNodes(ImVector<ImGridEntry *> nodes,
                                      bool upwards);

void GridPackEntries(ImGridEngine &ctx);

ImGridEntry *GridCopyPosition(ImGridEntry *a, ImGridEntry *b,
                              bool include_minmax = false);
ImGridEntry *GridCopyPositionFromOpts(ImGridEntry *a, ImGridMoveOptions *b,
                                      bool include_minmax = false);
ImGridEntry *GridCopyPositionToOpts(ImGridEntry *b, ImGridMoveOptions *a,
                                    bool include_minmax = false);

ImGridEntry *GridDirectionCollideCoverage(ImGridEntry *entry,
                                          ImGridMoveOptions &opts,
                                          ImVector<ImGridEntry *> &collides);

bool GridUseEntireRowArea(ImGridEngine &ctx, ImGridEntry *entry,
                          ImGridPosition new_position);

bool GridFixCollisions(ImGridEngine &ctx, ImGridEntry *entry,
                       ImGridPosition new_position, // = entry->Position,
                       ImGridEntry *collide = NULL,
                       ImGridMoveOptions opts = {});

bool GridMoveNode(ImGridEngine &ctx, ImGridEntry *entry,
                  ImGridMoveOptions &opts);

ImGridEntry *GridAddNode(ImGridEngine &ctx, ImGridEntry *entry,
                         bool trigger_add_event = false,
                         ImGridEntry *after = NULL);

void GridRemoveEntry(ImGridEngine &ctx, ImGridEntry *entry,
                     bool trigger_event = false);

bool GridChangedPosConstrain(ImGridEntry *entry, ImGridPosition &p);

int GridGetRow(ImGridEngine &ctx);

bool GridEntryMoveCheck(ImGridEngine &ctx, ImGridEntry *entry,
                        ImGridMoveOptions opts);

void GridCleanNodes(ImGridEngine &ctx);

void GridSaveInitial(ImGridEngine &ctx);

void GridBatchUpdate(ImGridEngine &ctx, bool flag = true, bool do_pack = true);

void GridCacheLayout(ImGridEngine &ctx, ImVector<ImGridEntry *> nodes,
                     int column, bool clear = false);

void GridCompact(ImGridEngine &ctx,
                 ImGridColumnFlags opts = ImGridColumnFlags_Compact,
                 bool do_sort = true);

void GridColumnChanged(ImGridEngine &ctx, int previous_column, int column,
                       ImGridColumnOptions opts = ImGridColumnOptions{
                           ImGridColumnFlags_MoveScale});

ImVector<ImGridEntry *> GridGetDirtyNodes(ImGridEngine &ctx);

void GridLayoutsNodesChanged(ImGridEngine &ctx, ImVector<ImGridEntry *> &nodes);

void GridTriggerChangeEvent(ImGridEngine &ctx);
void GridTriggerAddEvent(ImGridEngine &ctx);
void GridTriggerRemoveEvent(ImGridEngine &ctx);

void GridBeginUpdate(ImGridEngine &ctx, ImGridEntry *node);
void GridEndUpdate(ImGridEngine &ctx);

void GridFindSpace(ImGridEngine &ctx, ImGridEntry *entry,
                   ImVector<ImGridEntry *> &node_list, int column,
                   ImGridEntry *after = NULL);

} // namespace ImGrid::Engine
