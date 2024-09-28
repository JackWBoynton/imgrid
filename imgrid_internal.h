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

using ScreenSpace = float;
using GridSpace = int;

template <typename UnderlyingType> struct TypedImVec2 {
  UnderlyingType x, y;
  constexpr TypedImVec2() : x(), y() {}
  constexpr TypedImVec2(ImVec2 vec) : x(vec.x), y(vec.y) {}
  constexpr TypedImVec2(UnderlyingType _x, UnderlyingType _y) : x(_x), y(_y) {}
  UnderlyingType &operator[](size_t idx) {
    IM_ASSERT(idx == 0 || idx == 1);
    return ((UnderlyingType *)(void *)(char *)this)[idx];
  } // We very rarely use this [] operator, so the assert overhead is fine.
  UnderlyingType operator[](size_t idx) const {
    IM_ASSERT(idx == 0 || idx == 1);
    return ((const UnderlyingType *)(const void *)(const char *)this)[idx];
  }
  // conversion to ImVec2
  operator ImVec2() const { return ImVec2(x, y); }
};

template <typename UnderlyingType>
static inline TypedImVec2<UnderlyingType>
operator*(const TypedImVec2<UnderlyingType> &lhs, const UnderlyingType rhs) {
  return TypedImVec2<UnderlyingType>(lhs.x * rhs, lhs.y * rhs);
}

template <typename UnderlyingType>
static inline TypedImVec2<UnderlyingType>
operator/(const TypedImVec2<UnderlyingType> &lhs, const UnderlyingType rhs) {
  return TypedImVec2<UnderlyingType>(lhs.x / rhs, lhs.y / rhs);
}
template <typename UnderlyingType>
static inline TypedImVec2<UnderlyingType>
operator+(const TypedImVec2<UnderlyingType> &lhs,
          const TypedImVec2<UnderlyingType> &rhs) {
  return TypedImVec2<UnderlyingType>(lhs.x + rhs.x, lhs.y + rhs.y);
}
template <typename UnderlyingType>
static inline TypedImVec2<UnderlyingType>
operator-(const TypedImVec2<UnderlyingType> &lhs,
          const TypedImVec2<UnderlyingType> &rhs) {
  return TypedImVec2<UnderlyingType>(lhs.x - rhs.x, lhs.y - rhs.y);
}
template <typename UnderlyingType>
static inline TypedImVec2<UnderlyingType>
operator*(const TypedImVec2<UnderlyingType> &lhs,
          const TypedImVec2<UnderlyingType> &rhs) {
  return TypedImVec2<UnderlyingType>(lhs.x * rhs.x, lhs.y * rhs.y);
}
template <typename UnderlyingType>
static inline TypedImVec2<UnderlyingType>
operator/(const TypedImVec2<UnderlyingType> &lhs,
          const TypedImVec2<UnderlyingType> &rhs) {
  return TypedImVec2<UnderlyingType>(lhs.x / rhs.x, lhs.y / rhs.y);
}
template <typename UnderlyingType>
static inline TypedImVec2<UnderlyingType>
operator-(const TypedImVec2<UnderlyingType> &lhs) {
  return TypedImVec2<UnderlyingType>(-lhs.x, -lhs.y);
}
template <typename UnderlyingType>
static inline TypedImVec2<UnderlyingType> &
operator*=(TypedImVec2<UnderlyingType> &lhs, const UnderlyingType rhs) {
  lhs.x *= rhs;
  lhs.y *= rhs;
  return lhs;
}
template <typename UnderlyingType>
static inline TypedImVec2<UnderlyingType> &
operator/=(TypedImVec2<UnderlyingType> &lhs, const UnderlyingType rhs) {
  lhs.x /= rhs;
  lhs.y /= rhs;
  return lhs;
}
template <typename UnderlyingType>
static inline TypedImVec2<UnderlyingType> &
operator+=(TypedImVec2<UnderlyingType> &lhs,
           const TypedImVec2<UnderlyingType> &rhs) {
  lhs.x += rhs.x;
  lhs.y += rhs.y;
  return lhs;
}
template <typename UnderlyingType>
static inline TypedImVec2<UnderlyingType> &
operator-=(TypedImVec2<UnderlyingType> &lhs,
           const TypedImVec2<UnderlyingType> &rhs) {
  lhs.x -= rhs.x;
  lhs.y -= rhs.y;
  return lhs;
}
template <typename UnderlyingType>
static inline TypedImVec2<UnderlyingType> &
operator*=(TypedImVec2<UnderlyingType> &lhs,
           const TypedImVec2<UnderlyingType> &rhs) {
  lhs.x *= rhs.x;
  lhs.y *= rhs.y;
  return lhs;
}
template <typename UnderlyingType>
static inline TypedImVec2<UnderlyingType> &
operator/=(TypedImVec2<UnderlyingType> &lhs,
           const TypedImVec2<UnderlyingType> &rhs) {
  lhs.x /= rhs.x;
  lhs.y /= rhs.y;
  return lhs;
}
template <typename UnderlyingType>
static inline bool operator==(const TypedImVec2<UnderlyingType> &lhs,
                              const TypedImVec2<UnderlyingType> &rhs) {
  return lhs.x == rhs.x && lhs.y == rhs.y;
}
template <typename UnderlyingType>
static inline bool operator!=(const TypedImVec2<UnderlyingType> &lhs,
                              const TypedImVec2<UnderlyingType> &rhs) {
  return lhs.x != rhs.x || lhs.y != rhs.y;
}

template <typename Vec2DType> struct TypedImRect {
  Vec2DType Min; // Upper-left
  Vec2DType Max; // Lower-right

  constexpr TypedImRect() : Min(0.0f, 0.0f), Max(0.0f, 0.0f) {}
  constexpr TypedImRect(const Vec2DType &min, const Vec2DType &max)
      : Min(min), Max(max) {}
  constexpr TypedImRect(const ImVec4 &v) : Min(v.x, v.y), Max(v.z, v.w) {}
  constexpr TypedImRect(float x1, float y1, float x2, float y2)
      : Min(x1, y1), Max(x2, y2) {}

  Vec2DType GetCenter() const {
    return Vec2DType((Min.x + Max.x) * 0.5f, (Min.y + Max.y) * 0.5f);
  }
  Vec2DType GetSize() const { return Vec2DType(Max.x - Min.x, Max.y - Min.y); }
  float GetWidth() const { return Max.x - Min.x; }
  float GetHeight() const { return Max.y - Min.y; }
  float GetArea() const { return (Max.x - Min.x) * (Max.y - Min.y); }
  Vec2DType GetTL() const { return Min; }                     // Top-left
  Vec2DType GetTR() const { return Vec2DType(Max.x, Min.y); } // Top-right
  Vec2DType GetBL() const { return Vec2DType(Min.x, Max.y); } // Bottom-left
  Vec2DType GetBR() const { return Max; }                     // Bottom-right
  bool Contains(const Vec2DType &p) const {
    return p.x >= Min.x && p.y >= Min.y && p.x < Max.x && p.y < Max.y;
  }
  bool Contains(const TypedImRect<Vec2DType> &r) const {
    return r.Min.x >= Min.x && r.Min.y >= Min.y && r.Max.x <= Max.x &&
           r.Max.y <= Max.y;
  }
  bool ContainsWithPad(const Vec2DType &p, const Vec2DType &pad) const {
    return p.x >= Min.x - pad.x && p.y >= Min.y - pad.y &&
           p.x < Max.x + pad.x && p.y < Max.y + pad.y;
  }
  bool Overlaps(const TypedImRect<Vec2DType> &r) const {
    return r.Min.y < Max.y && r.Max.y > Min.y && r.Min.x < Max.x &&
           r.Max.x > Min.x;
  }
  void Add(const Vec2DType &p) {
    if (Min.x > p.x)
      Min.x = p.x;
    if (Min.y > p.y)
      Min.y = p.y;
    if (Max.x < p.x)
      Max.x = p.x;
    if (Max.y < p.y)
      Max.y = p.y;
  }
  void Add(const TypedImRect<Vec2DType> &r) {
    if (Min.x > r.Min.x)
      Min.x = r.Min.x;
    if (Min.y > r.Min.y)
      Min.y = r.Min.y;
    if (Max.x < r.Max.x)
      Max.x = r.Max.x;
    if (Max.y < r.Max.y)
      Max.y = r.Max.y;
  }
  void Expand(const float amount) {
    Min.x -= amount;
    Min.y -= amount;
    Max.x += amount;
    Max.y += amount;
  }
  void Expand(const Vec2DType &amount) {
    Min.x -= amount.x;
    Min.y -= amount.y;
    Max.x += amount.x;
    Max.y += amount.y;
  }
  void Translate(const Vec2DType &d) {
    Min.x += d.x;
    Min.y += d.y;
    Max.x += d.x;
    Max.y += d.y;
  }
  void TranslateX(float dx) {
    Min.x += dx;
    Max.x += dx;
  }
  void TranslateY(float dy) {
    Min.y += dy;
    Max.y += dy;
  }
  void ClipWith(const TypedImRect<Vec2DType> &r) {
    Min = ImMax(Min, r.Min);
    Max = ImMin(Max, r.Max);
  } // Simple version, may lead to an inverted rectangle, which is fine for
    // Contains/Overlaps test but not for display.
  void ClipWithFull(const TypedImRect<Vec2DType> &r) {
    Min = ImClamp(Min, r.Min, r.Max);
    Max = ImClamp(Max, r.Min, r.Max);
  } // Full version, ensure both points are fully clipped.
  void Floor() {
    Min.x = IM_TRUNC(Min.x);
    Min.y = IM_TRUNC(Min.y);
    Max.x = IM_TRUNC(Max.x);
    Max.y = IM_TRUNC(Max.y);
  }
  bool IsInverted() const { return Min.x > Max.x || Min.y > Max.y; }
  ImVec4 ToVec4() const { return ImVec4(Min.x, Min.y, Max.x, Max.y); }
};

using ScreenSpacePosition = TypedImVec2<ScreenSpace>;
using ScreenSpaceRect = TypedImRect<ScreenSpacePosition>;

using GridSpacePosition = TypedImVec2<GridSpace>;
using GridSpaceRect = TypedImRect<GridSpacePosition>;

struct ImGridClickInteractionState {

  ImGridClickInteractionType Type;

  struct {
    ScreenSpaceRect Rect;
  } BoxSelector;

  ImGridClickInteractionState() : Type(ImGridClickInteractionType_None) {}
};

struct ImGridEntry {
  int Id;

  // GRID VALUES
  ImGridPosition Position;
  ImGridEngine *ParentContext;

  bool AutoPosition;
  float MinW, MinH;
  float MaxW, MaxH;
  bool NoResize;
  bool NoMove;
  bool Locked;
  bool Resizable;

  bool AutoSize;

  bool Dirty;
  bool Updating;
  bool SkipDown;
  ImGridPosition PrevPosition;
  ImGridPosition Rect;
  ScreenSpacePosition LastUIPosition;
  ImGridPosition LastTried;
  ImGridPosition WillFitPos;

  // if Moving, use the moving position since we should drag smoothly, not in
  // grid steps
  ScreenSpacePosition MovingPosition;
  bool Moving;

  // if HasPreview, we render another rect at the PreviewPosition to show where
  // this node will snap to if it is dropped
  ScreenSpacePosition PreviewPosition;
  bool HasPreview;

  bool BorderHovered;
  bool BorderHeld;

  ScreenSpacePosition MoveMouseOffsetRel;

  struct {
    ImU32 Background, BackgroundHovered, BackgroundSelected, Outline, Titlebar,
        TitlebarHovered, TitlebarSelected, PreviewFill, PreviewOutline;
  } ColorStyle;

  struct {
    float CornerRounding;
    ImVec2 Padding;
    float BorderThickness;
  } LayoutStyle;

  // Position is in integer grid coordinates, so we need to get

  ImGridEntry(const int id, ImGridPosition ps);
  ImGridEntry(const int id);
  ImGridEntry(ImGridPosition pos);
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

  ScreenSpacePosition Panning;
  ScreenSpacePosition AutoPanningDelta;

  float Zoom;
  bool CtrlKeyHeld;
  float MouseWheelDelta;

  ScreenSpaceRect GridContentBounds;

  ImGridClickInteractionState ClickInteraction;

  ImDrawList *CanvasDrawList;

  ScreenSpacePosition CanvasOriginScreenSpace;
  ScreenSpaceRect CanvasRectScreenSpace;

  ImGuiStorage EntryIdxToSubmissionIdx;
  ImVector<int> EntryIdxSubmissionOrder;
  ImVector<int> EntryIndicesOverlappingWithMouse;
  ImVector<int> EntryTitleBarIndicesOverlappingWithMouse;

  ImVector<int> EntryDepthOrder;

  ImVector<int> SelectedEntryIndices;
  // Relative origins of selected nodes for snapping of dragged nodes
  ImVector<ImVec2> SelectedEntryOffsets;
  // Offset of the primary node origin relative to the mouse cursor.
  ScreenSpacePosition PrimaryEntryOffset;

  ImGridScope CurrentScope;

  ImGridIO IO;
  ImGridStyle Style;
  ImVector<ImGridColElement> ColorModifierStack;
  ImVector<ImGridStyleVarElement> StyleModifierStack;

  int CurrentEntryIdx;

  ImOptionalIndex HoveredEntryIdx;
  ImOptionalIndex HoveredEntryTitleBarIdx;

  ScreenSpacePosition MousePos;

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

static inline ScreenSpaceRect
GetNodePreviewScreenRect(const ImGridContext &ctx, const ImGridEntry &entry) {
  // converts from grid coordinates (integer) to the screen coordinates (using
  // the grid size and node size)
  const ScreenSpacePosition grid_origin_screen_space(
      ctx.CanvasOriginScreenSpace);

  const float grid_size = ctx.Style.GridSpacing;

  const ImVec2 node_size(entry.Position.w * grid_size,
                         entry.Position.h * grid_size);

  ScreenSpacePosition node_pos =
      ImVec2(entry.Position.x * grid_size + ctx.Panning.x,
             entry.Position.y * grid_size + ctx.Panning.y);
  return ScreenSpaceRect(grid_origin_screen_space + node_pos,
                         grid_origin_screen_space + node_pos + node_size);
}

static inline ScreenSpaceRect GetNodeScreenRect(const ImGridContext &ctx,
                                                const ImGridEntry &entry) {
  // converts from grid coordinates (integer) to the screen coordinates (using
  // the grid size and node size)
  const ScreenSpacePosition grid_origin_screen_space(
      ctx.CanvasOriginScreenSpace);
  printf("grid_origin_screen_space: %f, %f\n", grid_origin_screen_space.x,
         grid_origin_screen_space.y);

  const float grid_size = ctx.Style.GridSpacing;

  const ImVec2 node_size(entry.Position.w * grid_size,
                         entry.Position.h * grid_size);

  ScreenSpacePosition node_pos;
  if (entry.Moving) {
    node_pos =
        entry.MovingPosition + ctx.Panning; // - entry.MoveMouseOffsetRel;
  } else {
    node_pos = ImVec2(entry.Position.x * grid_size + ctx.Panning.x,
                      entry.Position.y * grid_size + ctx.Panning.y);
  }
  return ScreenSpaceRect(grid_origin_screen_space + node_pos,
                         grid_origin_screen_space + node_pos + node_size);
}

static inline void UpdateNodeGridSpaceSize(ImGridContext &ctx,
                                           ImGridEntry &entry,
                                           float width_pixels,
                                           float height_pixels) {
  // converts from screen space item width/height to grid coordinates w, h
  const float grid_size = ctx.Style.GridSpacing;
  entry.Position.w = std::max(1.0f, std::ceilf((width_pixels / grid_size) + 1));
  entry.Position.h =
      std::max(1.0f, std::ceilf((height_pixels / grid_size) + 1));
}

} // namespace ImGrid
