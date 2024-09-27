#pragma once

#define IMGUI_DEFINE_MATH_OPERATORS
#include <imgui.h>
#include <imgui_internal.h>

#include "imgrid.h"
#include "imgrid_grid_engine.h"

#include <limits.h>
#include <map>

#define IM_MIN(x, y) x > y ? y : x
#define IM_MAX(x, y) x > y ? x : y

struct ImGridContext;

// from imgrid_grid_internal.h
struct ImGridEntryData;
struct ImGridPosition;
struct ImGridEngine;

extern ImGridContext *GImGrid;

typedef int ImGridScope;
typedef int ImGridClickInteractionType;

enum ImGridScope_ {
  ImGridScope_None = 1,
  ImGridScope_Grid = 1 << 1,
  ImGridScope_Entry = 1 << 2,
};

enum ImGridClickInteractionType_ {
  ImGridClickInteractionType_None = 1,
  ImGridClickInteractionType_Entry = 1 << 1,
  ImGridClickInteractionType_ImGuiItem = 1 << 2,
  ImGridClickInteractionType_Resizing = 1 << 3,
  ImGridClickInteractionType_BoxSelection = 1 << 4,
  ImGridClickInteractionType_Panning = 1 << 5,
};

// [SECTION] internal data structures
// from ImNodes

// The object T must have the following interface:
//
// struct T
// {
//     T();
//
//     int id;
// };
template <typename T> struct ImObjectPool {
  ImVector<T> Pool;
  ImVector<bool> InUse;
  ImVector<int> FreeList;
  ImGuiStorage IdMap;

  ImObjectPool() : Pool(), InUse(), FreeList(), IdMap() {}
};

// Emulates std::optional<int> using the sentinel value `INVALID_INDEX`.
struct ImOptionalIndex {
  ImOptionalIndex() : _Index(INVALID_INDEX) {}
  ImOptionalIndex(const int value) : _Index(value) {}

  // Observers

  inline bool HasValue() const { return _Index != INVALID_INDEX; }

  inline int Value() const {
    IM_ASSERT(HasValue());
    return _Index;
  }

  // Modifiers

  inline ImOptionalIndex &operator=(const int value) {
    _Index = value;
    return *this;
  }

  inline void Reset() { _Index = INVALID_INDEX; }

  inline bool operator==(const ImOptionalIndex &rhs) const {
    return _Index == rhs._Index;
  }

  inline bool operator==(const int rhs) const { return _Index == rhs; }

  inline bool operator!=(const ImOptionalIndex &rhs) const {
    return _Index != rhs._Index;
  }

  inline bool operator!=(const int rhs) const { return _Index != rhs; }

  static const int INVALID_INDEX = -1;

private:
  int _Index;
};

struct ImGridClickInteractionState {

  ImGridClickInteractionType Type;

  struct {
    ImRect Rect; // Coordinates in grid space
  } BoxSelector;

  ImGridClickInteractionState() : Type(ImGridClickInteractionType_None) {}
};

struct ImGridEntry {
  int Id;
  ImVec2 Origin;
  ImRect Rect;
  ImVec2 DummySize;
  ImRect TitleBarContentRect;

  ImGridEntryData GridData;

  bool Draggable;
  bool Resizable;
  bool Locked;
  bool Moving;

  ImRect PreviewRect;
  bool HasPreview;
  bool PreviewHeld;
  bool PreviewHovered;

  ImRect CachedItemRect;

  struct {
    ImU32 Background, BackgroundHovered, BackgroundSelected, Outline, Titlebar,
        TitlebarHovered, TitlebarSelected, PreviewFill, PreviewOutline;
  } ColorStyle;

  struct {
    float CornerRounding;
    ImVec2 Padding;
    float BorderThickness;
  } LayoutStyle;

  ImGridEntry(const int id);
  ~ImGridEntry() { Id = INT_MIN; }
};

struct ImGridColElement {
  ImU32 Color;
  ImGridCol Item;

  ImGridColElement(const ImU32 c, const ImGridCol s) : Color(c), Item(s) {}
};

struct ImGridStyleVarElement {
  ImGridStyleVar Item;
  float FloatValue[2];

  ImGridStyleVarElement(const ImGridStyleVar variable, const float value)
      : Item(variable) {
    FloatValue[0] = value;
  }

  ImGridStyleVarElement(const ImGridStyleVar variable, const ImVec2 value)
      : Item(variable) {
    FloatValue[0] = value.x;
    FloatValue[1] = value.y;
  }
};

struct ImGridContext {
  ImObjectPool<ImGridEntry> Entries;

  ImVec2 Panning;
  ImVec2 AutoPanningDelta;

  float Zoom;

  ImRect GridContentBounds;

  ImGridClickInteractionState ClickInteraction;

  ImDrawList *CanvasDrawList;

  ImVec2 CanvasOriginScreenSpace;
  ImRect CanvasRectScreenSpace;

  ImGuiStorage EntryIdxToSubmissionIdx;
  ImVector<int> EntryIdxSubmissionOrder;
  ImVector<int> EntryIndicesOverlappingWithMouse;
  ImVector<int> EntryTitleBarIndicesOverlappingWithMouse;

  ImVector<int> EntryDepthOrder;

  ImVector<int> SelectedEntryIndices;
  // Relative origins of selected nodes for snapping of dragged nodes
  ImVector<ImVec2> SelectedEntryOffsets;
  // Offset of the primary node origin relative to the mouse cursor.
  ImVec2 PrimaryEntryOffset;

  ImGridScope CurrentScope;

  ImGridIO IO;
  ImGridStyle Style;
  ImVector<ImGridColElement> ColorModifierStack;
  ImVector<ImGridStyleVarElement> StyleModifierStack;

  int CurrentEntryIdx;

  ImOptionalIndex HoveredEntryIdx;
  ImOptionalIndex HoveredEntryTitleBarIdx;

  ImVec2 MousePos;

  bool LeftMouseClicked;
  bool LeftMouseReleased;
  bool AltMouseClicked;
  bool LeftMouseDragging;
  bool AltMouseDragging;
  float AltMouseScrollDelta;
  bool MultipleSelectModifier;

  float GridHeight;

  ImGridEngine *Engine;
};

namespace ImGrid {

static inline ImGridContext &Context() {
  // No Context was set! Did you forget to call ImGrid::CreateContext()?
  IM_ASSERT(GImGrid);
  return *GImGrid;
}

// [SECTION] ObjectPool implementation
// from ImNodes

template <typename T>
static inline int ObjectPoolFind(const ImObjectPool<T> &objects, const int id) {
  const int index = objects.IdMap.GetInt(static_cast<ImGuiID>(id), -1);
  return index;
}

template <typename T>
static inline void ObjectPoolUpdate(ImObjectPool<T> &objects) {
  for (int i = 0; i < objects.InUse.size(); ++i) {
    const int id = objects.Pool[i].Id;

    if (!objects.InUse[i] && objects.IdMap.GetInt(id, -1) == i) {
      objects.IdMap.SetInt(id, -1);
      objects.FreeList.push_back(i);
      (objects.Pool.Data + i)->~T();
    }
  }
}

template <> inline void ObjectPoolUpdate(ImObjectPool<ImGridEntry> &nodes) {
  for (int i = 0; i < nodes.InUse.size(); ++i) {
    if (!nodes.InUse[i]) {
      const int id = nodes.Pool[i].Id;

      if (nodes.IdMap.GetInt(id, -1) == i) {
        // Remove node idx form depth stack the first time we detect that this
        // idx slot is unused
        ImVector<int> &depth_stack = GImGrid->EntryDepthOrder;
        const int *const elem = depth_stack.find(i);
        IM_ASSERT(elem != depth_stack.end());
        depth_stack.erase(elem);

        nodes.IdMap.SetInt(id, -1);
        nodes.FreeList.push_back(i);
        (nodes.Pool.Data + i)->~ImGridEntry();
      }
    }
  }
}

template <typename T>
static inline void ObjectPoolReset(ImObjectPool<T> &objects) {
  if (!objects.InUse.empty()) {
    memset(objects.InUse.Data, 0, objects.InUse.size_in_bytes());
  }
}

template <typename T>
static inline int ObjectPoolFindOrCreateIndex(ImObjectPool<T> &objects,
                                              const int id) {
  int index = objects.IdMap.GetInt(static_cast<ImGuiID>(id), -1);

  // Construct new object
  if (index == -1) {
    if (objects.FreeList.empty()) {
      index = objects.Pool.size();
      IM_ASSERT(objects.Pool.size() == objects.InUse.size());
      const int new_size = objects.Pool.size() + 1;
      objects.Pool.resize(new_size);
      objects.InUse.resize(new_size);
    } else {
      index = objects.FreeList.back();
      objects.FreeList.pop_back();
    }
    IM_PLACEMENT_NEW(objects.Pool.Data + index) T(id);
    objects.IdMap.SetInt(static_cast<ImGuiID>(id), index);
  }

  // Flag it as used
  objects.InUse[index] = true;

  return index;
}

template <>
inline int ObjectPoolFindOrCreateIndex(ImObjectPool<ImGridEntry> &nodes,
                                       const int node_id) {
  int node_idx = nodes.IdMap.GetInt(static_cast<ImGuiID>(node_id), -1);

  // Construct new node
  if (node_idx == -1) {
    if (nodes.FreeList.empty()) {
      node_idx = nodes.Pool.size();
      IM_ASSERT(nodes.Pool.size() == nodes.InUse.size());
      const int new_size = nodes.Pool.size() + 1;
      nodes.Pool.resize(new_size);
      nodes.InUse.resize(new_size);
    } else {
      node_idx = nodes.FreeList.back();
      nodes.FreeList.pop_back();
    }
    IM_PLACEMENT_NEW(nodes.Pool.Data + node_idx) ImGridEntry(node_id);
    nodes.IdMap.SetInt(static_cast<ImGuiID>(node_id), node_idx);

    GImGrid->EntryDepthOrder.push_back(node_idx);
  }

  // Flag node as used
  nodes.InUse[node_idx] = true;

  return node_idx;
}

template <typename T>
static inline T &ObjectPoolFindOrCreateObject(ImObjectPool<T> &objects,
                                              const int id) {
  const int index = ObjectPoolFindOrCreateIndex(objects, id);
  return objects.Pool[index];
}

} // namespace ImGrid
