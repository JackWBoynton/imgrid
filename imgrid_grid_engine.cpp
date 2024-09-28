
#include "imgrid_grid_engine.h"
#include "imgrid_internal.h"

#include <cmath>

inline bool GridPositionsAreIntercepted(ImGridPosition a, ImGridPosition b) {
  return !(a.y >= b.y + b.h || a.y + a.h <= b.y || a.x + a.w <= b.x ||
           a.x >= b.x + b.w);
}

inline bool RectsAreTouching(ImGridEntry &a, ImGridEntry &b) {
  return GridPositionsAreIntercepted(a.Position,
                                     {b.Position.x - 0.5f, b.Position.y - 0.5f,
                                      b.Position.w + 1.f, b.Position.h + 1.f});
}

inline bool SwapEntryPositions(ImGridEntry &a, ImGridEntry &b) {
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

  std::optional<bool> touching;
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

bool GridFindEmptyPosition(ImGridEngine &ctx, ImGridEntry &entry, int column,
                           ImVector<ImGridEntry *> &entries,
                           ImGridEntry *after) {

  int start = 0;
  if (after != NULL)
    start =
        after->Position.y * column + (after->Position.x + after->Position.w);

  bool found = false;
  const int maxIterations = ctx.Column * ctx.MaxRow;

  for (int i = start; !found && i < maxIterations; ++i) {
    int x = i % column;
    int y = i / column;
    if (x + entry.Position.w > column)
      continue;
    ImGridPosition box = {static_cast<float>(x), static_cast<float>(y),
                          entry.Position.w, entry.Position.h};
    bool intercepted = false;
    for (int j = 0; j < entries.size(); ++j) {
      if (GridPositionsAreIntercepted(box, entries[j]->Position)) {
        intercepted = true;
        break;
      }
    }
    if (!intercepted) {
      if (entry.Position.x != x || entry.Position.y != y)
        entry.Dirty = true;

      entry.Position.x = x;
      entry.Position.y = y;
      found = true;
    }
  }
  return found;
}

int GridFindCacheLayout(ImGridEngine &ctx, ImGridEntry *node, int column) {
  int i = 0;
  for (const auto &cache_node : ctx.CacheLayouts[column]) {
    if (cache_node.Id == node->Id)
      return i;
    i++;
  }
  return -1;
}

void GridCacheOneLayout(ImGridEngine &ctx, ImGridEntry *entry, int column) {

  if (!(entry->Position.x < 119 && entry->Position.y < 119)) {
    IM_ASSERT(false);
  }
  ImGridEntry wrapped = {
      ImGridPosition{entry->Position.x, entry->Position.y, entry->Position.w,
                     -1},
  };
  if (entry->AutoPosition || entry->Position.x == -1) {
    entry->Position.x = -1;
    entry->Position.y = -1;
    if (entry->AutoPosition)
      wrapped.AutoPosition = true;
  }

  int index = GridFindCacheLayout(ctx, entry, column);
  if (index < 0)
    ctx.CacheLayouts[column].push_back(wrapped);
  else
    ctx.CacheLayouts[column][index] = wrapped;
}

void GridNodeBoundFix(ImGridEngine &ctx, ImGridEntry *entry, bool resizing) {
  if (!(entry->Position.x < 119 && entry->Position.y < 119)) {
    IM_ASSERT(false);
  }
  ImGridPosition pre = entry->PrevPosition;
  if (!pre.Valid()) {
    pre.x = entry->Position.x;
    pre.y = entry->Position.y;
    pre.w = entry->Position.w;
    pre.h = entry->Position.h;
  }

  if (entry->MaxW > 0)
    entry->Position.w = IM_MIN(entry->MaxW, entry->Position.w);
  if (entry->MaxH > 0)
    entry->Position.h = IM_MIN(entry->MaxH, entry->Position.h);
  if (entry->MinW > 0 && entry->MinW <= ctx.Column)
    entry->Position.w = IM_MAX(entry->MinW, entry->Position.w);
  if (entry->MinH > 0)
    entry->Position.h = IM_MAX(entry->MinH, entry->Position.h);

  const bool save_orig = (entry->Position.x >= 0 ? entry->Position.x : 0) +
                             (entry->Position.w >= 0 ? entry->Position.w : 1) >
                         ctx.Column;
  if (save_orig && ctx.Column < 12 && !ctx.InColumnResize &&
      GridFindCacheLayout(ctx, entry, 12) == -1) {
    ImGridEntry copy = *entry;
    if (copy.AutoPosition || copy.Position.x == -1) {
      copy.Position.x = -1;
      copy.Position.y = -1;
    } else {
      copy.Position.x = IM_MIN(copy.Position.x, 12 - 1);
    }
    copy.Position.w = IM_MIN(copy.Position.w != -1 ? copy.Position.w : 1, 12);
    GridCacheOneLayout(ctx, entry, 12);
  }

  if (entry->Position.w > ctx.Column)
    entry->Position.w = ctx.Column;
  else if (entry->Position.w < 1)
    entry->Position.w = 1;

  if (ctx.MaxRow > 0 && entry->Position.h > ctx.MaxRow)
    entry->Position.h = ctx.MaxRow;
  else if (entry->Position.h < 1)
    entry->Position.h = 1;

  entry->Position.x = IM_MAX(entry->Position.x, 0);
  entry->Position.y = IM_MAX(entry->Position.y, 0);

  if (entry->Position.x + entry->Position.w > ctx.Column) {
    if (resizing)
      entry->Position.w = ctx.Column - entry->Position.x;
    else
      entry->Position.x = ctx.Column - entry->Position.w;
  }

  if (ctx.MaxRow > 0 && entry->Position.y + entry->Position.h > ctx.MaxRow) {
    if (resizing)
      entry->Position.h = ctx.MaxRow - entry->Position.y;
    else
      entry->Position.y = ctx.MaxRow - entry->Position.h;
  }

  if (entry->Position != pre)
    entry->Dirty = true;
}

void GridResizeToContentCheck(ImGridEngine &ctx, bool delay,
                              ImGridEntry *entry) {
  (void)delay;
  (void)entry;
  (void)ctx;
  // TODO: handle delay/anim
}

ImGridEntry *GridPrepareEntry(ImGridEngine &ctx, ImGridEntry *entry,
                              bool resizing) {
  if (!(entry->Position.x < 119 && entry->Position.y < 119)) {
    IM_ASSERT(false);
  }

  if (entry->Position.h == -1 || entry->Position.w == -1)
    IM_ASSERT(false);

  if (entry->Position.x == -1 || entry->Position.y == -1)
    entry->AutoPosition = true;

  ImGridPosition def = ImGridPosition{0, 0, 1, 1};
  entry->Position.SetDefault(def);

  GridNodeBoundFix(ctx, entry, resizing);
  return entry;
}

ImVector<ImGridEntry *> GridGetDirtyNodes(ImGridEngine &ctx) {
  ImVector<ImGridEntry *> dirty_nodes;
  for (auto &entry : ctx.Entries) {
    if (entry->Dirty)
      dirty_nodes.push_back(entry);
  }
  return dirty_nodes;
}

void GridLayoutsNodesChanged(ImGridEngine &ctx,
                             ImVector<ImGridEntry *> &nodes) {
  if (ctx.CacheLayouts.size() == 0 || ctx.InColumnResize)
    return;

  for (auto &[column, layout] : ctx.CacheLayouts) {
    if (layout.size() == 0 || column == ctx.Column)
      continue;
    if (column < ctx.Column) {
      ctx.CacheLayouts.erase(column);
    } else {
      float ratio = column / static_cast<float>(ctx.Column);
      for (auto &entry : layout) {
        if (!entry.PrevPosition.Valid())
          continue;
        ImGridEntry *node = NULL;
        for (auto &n : nodes) {
          if (n->Id == entry.Id) {
            node = n;
            break;
          }
        }
        if (node == NULL)
          continue;
        if (node->Position.y >= 0 && node->Position.y != node->PrevPosition.y) {
          node->Position.y += (node->Position.y - node->PrevPosition.y);
        }
        if (node->Position.x != node->PrevPosition.x) {
          node->Position.x = std::round(node->Position.x * ratio);
        }

        if (node->Position.w != node->PrevPosition.w) {
          node->Position.w = std::round(node->Position.w * ratio);
        }
      }
    }
  }
}

ImGridEntry *GridCollide(ImGridEngine &ctx, ImGridEntry *skip,
                         ImGridPosition area, ImGridEntry *skip2) {
  const auto skip_id = skip->Id;
  const auto skip2_id = skip2 == NULL ? -1 : skip2->Id;
  for (const auto &entry : ctx.Entries) {
    if (entry->Id != skip_id && entry->Id != skip2_id &&
        GridPositionsAreIntercepted(entry->Position, area))
      return entry;
  }
  return NULL;
}

ImVector<ImGridEntry *> GridCollideAll(ImGridEngine &ctx, ImGridEntry *skip,
                                       ImGridPosition area,
                                       ImGridEntry *skip2) {
  ImVector<ImGridEntry *> collided;
  IM_ASSERT(skip != NULL);
  const auto skip_id = skip->Id;
  const auto skip2_id = skip2 == NULL ? -1 : skip2->Id;
  for (const auto &entry : ctx.Entries) {
    if (entry->Id != skip_id && entry->Id != skip2_id &&
        GridPositionsAreIntercepted(entry->Position, area))
      collided.push_back(entry);
  }
  return collided;
}

void GridSortNodesInplace(ImVector<ImGridEntry *> &nodes, bool upwards) {
  int direction = upwards ? -1 : 1;
  int und = 10000;

  std::sort(nodes.begin(), nodes.end(), [&](ImGridEntry *a, ImGridEntry *b) {
    auto diffY = direction * ((a->Position.y == -1 ? und : a->Position.y) -
                              (b->Position.y == -1 ? und : b->Position.y));
    if (diffY == 0)
      return direction * ((a->Position.x == -1 ? und : a->Position.x) -
                          (b->Position.x == -1 ? und : b->Position.x));
    return diffY;
  });
}

inline ImVector<ImGridEntry *> GridSortNodes(ImVector<ImGridEntry *> nodes,
                                             bool upwards) {
  ImVector<ImGridEntry *> sorted_nodes = nodes;
  GridSortNodesInplace(sorted_nodes, upwards);
  return sorted_nodes;
}

void GridTriggerChangeEvent(ImGridEngine &ctx) {
  if (ctx.BatchMode)
    return;

  auto dirty_nodes = GridGetDirtyNodes(ctx);
  if (dirty_nodes.size() > 0) {
    if (!ctx.IgnoreLayoutsNodeChange) {
      GridLayoutsNodesChanged(ctx, dirty_nodes);
    }
  }
  GridSaveInitial(ctx);
}

void GridTriggerAddEvent(ImGridEngine &ctx) {
  if (ctx.BatchMode)
    return;

  if (ctx.AddedEntries.size() > 0) {
    if (!ctx.IgnoreLayoutsNodeChange) {
      GridLayoutsNodesChanged(ctx, ctx.AddedEntries);
    }
  }

  for (auto &entry : ctx.AddedEntries) {
    entry->Dirty = false;
  }
}

void GridTriggerRemoveEvent(ImGridEngine &ctx) {
  if (ctx.BatchMode)
    return;
}

void GridPackEntries(ImGridEngine &ctx) {
  if (ctx.BatchMode)
    return;

  GridSortNodesInplace(ctx.Entries, true);

  if (ctx.Float) {
    for (auto &entry : ctx.Entries) {
      if (entry->Updating || !entry->PrevPosition.Valid() ||
          entry->Position.y == entry->PrevPosition.y)
        continue;

      auto newY = entry->Position.y;
      while (newY > entry->PrevPosition.y) {
        --newY;
        auto *collided = GridCollide(
            ctx, entry,
            {entry->Position.x, newY, entry->Position.w, entry->Position.h},
            NULL);
        if (collided == NULL) {
          entry->Dirty = true;
          entry->Position.y = newY;
        }
      }
    }
  } else {
    // top grav pack
    int index = 0;
    for (auto &entry : ctx.Entries) {
      if (entry->Locked)
        continue;
      while (entry->Position.y > 0) {
        auto newY = (index == 0) ? 0 : entry->Position.y - 1;
        const bool can_be_moved =
            (index == 0) || GridCollide(ctx, entry,
                                        {entry->Position.x, newY,
                                         entry->Position.w, entry->Position.h},
                                        NULL) == NULL;
        if (!can_be_moved)
          break;
        entry->Dirty = (entry->Position.y != newY);
        entry->Position.y = newY;
      }
      index++;
    }
  }
}

ImGridEntry *GridCopyPosition(ImGridEntry *a, ImGridEntry *b,
                              bool include_minmax) {
  IM_ASSERT(a != NULL);
  // this allocates a new temporary "Node" that is used to perform
  // transformations

  if (b->Position.x != -1)
    a->Position.x = b->Position.x;
  if (b->Position.y != -1)
    a->Position.y = b->Position.y;
  if (b->Position.w != -1)
    a->Position.w = b->Position.w;
  if (b->Position.h != -1)
    a->Position.h = b->Position.h;

  if (include_minmax) {
    a->MinW = b->MinW;
    a->MinH = b->MinH;
    a->MaxW = b->MaxW;
    a->MaxH = b->MaxH;
  }
  return a;
}

ImGridEntry *GridCopyPositionFromOpts(ImGridEntry *a, ImGridMoveOptions *b,
                                      bool include_minmax) {
  IM_ASSERT(a != NULL);
  // this allocates a new temporary "Node" that is used to perform
  // transformations

  if (b->Position.x != -1)
    a->Position.x = b->Position.x;
  if (b->Position.y != -1)
    a->Position.y = b->Position.y;
  if (b->Position.w != -1)
    a->Position.w = b->Position.w;
  if (b->Position.h != -1)
    a->Position.h = b->Position.h;

  if (include_minmax) {
    if (b->MinW != -1)
      a->MinW = b->MinW;
    if (b->MinH != -1)
      a->MinH = b->MinH;
    if (b->MaxW != -1)
      a->MaxW = b->MaxW;
    if (b->MaxH != -1)
      a->MaxH = b->MaxH;
  }
  return a;
}

ImGridEntry *GridCopyPositionToOpts(ImGridEntry *b, ImGridMoveOptions *a,
                                    bool include_minmax) {
  IM_ASSERT(a != NULL);
  // this allocates a new temporary "Node" that is used to perform
  // transformations

  if (b->Position.x != -1)
    a->Position.x = b->Position.x;
  if (b->Position.y != -1)
    a->Position.y = b->Position.y;
  if (b->Position.w != -1)
    a->Position.w = b->Position.w;
  if (b->Position.h != -1)
    a->Position.h = b->Position.h;

  if (include_minmax) {
    if (b->MinW != -1)
      a->MinW = b->MinW;
    if (b->MinH != -1)
      a->MinH = b->MinH;
    if (b->MaxW != -1)
      a->MaxW = b->MaxW;
    if (b->MaxH != -1)
      a->MaxH = b->MaxH;
  }
  return b;
}

ImGridEntry *GridDirectionCollideCoverage(ImGridEntry *entry,
                                          ImGridMoveOptions &opts,
                                          ImVector<ImGridEntry *> &collides) {

  if (!(entry->Position.x < 119 && entry->Position.y < 119)) {
    IM_ASSERT(false);
  }

  if (!entry->Rect || !opts.Rect)
    return NULL;

  ImGridPosition &r0 = entry->Rect;
  ImGridPosition &r = opts.Rect; // current dragged position
  if (r.y > r0.y) {
    r.h += r.y - r0.y;
    r.y = r0.y;
  } else {
    r.h += r0.y - r.y;
  }

  if (r.x > r0.x) {
    r.w += r.x - r0.x;
    r.x = r0.x;
  } else {
    r.w += r0.x - r.x;
  }

  ImGridEntry *collide = NULL;
  float over_max = 0.5f;
  for (auto &n : collides) {
    if (n->Locked || !n->Rect) {
      break;
    }
    ImGridPosition &r2 = n->Rect; // overlapping target
    float yOver = 9999.9f, xOver = 9999.9f;
    // depending on which side we started from, compute the overlap % of
    // coverage (ex: from above/below we only compute the max horizontal line
    // coverage)
    if (r0.y < r2.y) { // from above
      yOver = ((r.y + r.h) - r2.y) / r2.h;
    } else if (r0.y + r0.h > r2.y + r2.h) { // from below
      yOver = ((r2.y + r2.h) - r.y) / r2.h;
    }
    if (r0.x < r2.x) { // from the left
      xOver = ((r.x + r.w) - r2.x) / r2.w;
    } else if (r0.x + r0.w > r2.x + r2.w) { // from the right
      xOver = ((r2.x + r2.w) - r.x) / r2.w;
    }
    float over = IM_MIN(xOver, yOver);
    if (over > over_max) {
      over_max = over;
      collide = n;
    }
  }

  opts.Collide = collide;
  return collide;
}

bool GridUseEntireRowArea(ImGridEngine &ctx, ImGridEntry *entry,
                          ImGridPosition new_position) {

  if (!(entry->Position.x < 119 && entry->Position.y < 119)) {
    IM_ASSERT(false);
  }
  return (!ctx.Float || (ctx.BatchMode && !ctx.PrevFloat)) && !ctx.HasLocked &&
         (!entry->Moving || !entry->SkipDown ||
          new_position.y <= entry->Position.y);
}

bool GridMoveNode(ImGridEngine &ctx, ImGridEntry *entry,
                  ImGridMoveOptions &opts) {
  if (entry == NULL)
    return false;

  if (!(entry->Position.x < 119 && entry->Position.y < 119)) {
    IM_ASSERT(false);
  }

  // might be wrong...
  // bool was_undefined_pack;

  opts.Position.SetDefault(entry->Position);

  bool resizing = (entry->Position.w != opts.Position.w ||
                   entry->Position.h != opts.Position.h);
  ImGridEntry new_node(entry->Id);

  GridCopyPosition(&new_node, entry, true);
  GridCopyPositionFromOpts(&new_node, &opts);
  GridNodeBoundFix(ctx, &new_node, resizing);
  GridCopyPositionToOpts(&new_node, &opts);

  if (!opts.ForceCollide && entry->Position == opts.Position)
    return false;

  ImGridPosition prev_pos = entry->Position;
  opts.Skip = NULL;

  ImVector<ImGridEntry *> collided =
      GridCollideAll(ctx, entry, new_node.Position, opts.Skip);
  bool need_to_move = true;
  if (collided.size() > 0) {
    bool active_drag = entry->Moving && !opts.Nested;

    ImGridEntry *collide =
        active_drag ? GridDirectionCollideCoverage(entry, opts, collided)
                    : collided[0];
    // if (active_drag && collide != NULL &&
    /* if (activeDrag && collide && node.grid?.opts?.subGridDynamic &&
  !node.grid._isTemp) { let over = Utils.areaIntercept(o.rect,
  collide._rect); let a1 = Utils.area(o.rect); let a2 =
  Utils.area(collide._rect); let perc = over / (a1 < a2 ? a1 : a2); if (perc
  > .8) { collide.grid.makeSubGrid(collide.el, undefined, node); collide =
  undefined;
    }
  }
  */
    if (collide != NULL) {
      need_to_move =
          !GridFixCollisions(ctx, entry, new_node.Position, collide, opts);
    } else {
      need_to_move = false;
    }
  }

  if (need_to_move) {
    entry->Dirty = true;
    GridCopyPosition(entry, &new_node);
  }

  if (opts.Pack) {
    GridPackEntries(ctx);
  }

  return entry->Position == prev_pos;
}

bool GridFixCollisions(ImGridEngine &ctx, ImGridEntry *entry,
                       ImGridPosition new_position, // = entry->Position,
                       ImGridEntry *collide, ImGridMoveOptions opts) {

  if (!(entry->Position.x < 119 && entry->Position.y < 119)) {
    IM_ASSERT(false);
  }
  GridSortNodesInplace(ctx.Entries, true);

  collide =
      collide == NULL ? GridCollide(ctx, entry, new_position, NULL) : collide;
  if (collide == NULL)
    return false;

  if (entry->Moving && !opts.Nested && !ctx.Float) {
    if (SwapEntryPositions(*entry, *collide))
      return true;
  }

  ImGridPosition area = new_position;
  if (!ctx.Loading && GridUseEntireRowArea(ctx, entry, new_position)) {
    area = {0, new_position.y, static_cast<float>(ctx.Column), new_position.h};
    collide = GridCollide(ctx, entry, area, opts.Skip);
  }

  bool did_move = false;
  ImGridMoveOptions new_opts{};
  new_opts.Nested = true;
  new_opts.Pack = false;

  while (collide != NULL ||
         (collide = GridCollide(ctx, entry, area, opts.Skip))) {
    bool moved = false;

    if (collide->Locked || ctx.Loading ||
        (entry->Moving && !entry->SkipDown &&
         (new_position.y > entry->Position.y && !ctx.Float &&
          (GridCollide(ctx, collide,
                       {collide->Position.x, entry->Position.y,
                        collide->Position.w, collide->Position.h},
                       entry) == NULL ||
           GridCollide(ctx, collide,
                       {collide->Position.x,
                        new_position.y - collide->Position.h,
                        collide->Position.w, collide->Position.h},
                       entry) == NULL)))) {
      entry->SkipDown = entry->SkipDown || new_position.y > entry->Position.y;
      ImGridMoveOptions opt = new_opts;
      opt.Position = {new_position.x, collide->Position.y + collide->Position.h,
                      new_position.w, new_position.h};
      moved = GridMoveNode(ctx, entry, opt);
      if ((collide->Locked || ctx.Loading) && moved) {
        new_position = entry->Position;
      } else if (!collide->Locked && moved && opts.Pack) {
        GridPackEntries(ctx);
        new_position.y = collide->Position.y + collide->Position.h;
        entry->Position = new_position;
      }
      did_move = did_move || moved;
    } else {
      ImGridMoveOptions opt = new_opts;
      opt.Position = {collide->Position.x, new_position.y + new_position.h,
                      collide->Position.w, collide->Position.h};
      opt.Skip = entry;
      moved = GridMoveNode(ctx, collide, opt);
    }

    if (!moved)
      return did_move;

    collide = NULL;
  }
  return did_move;
}

ImGridEntry *GridAddNode(ImGridEngine &ctx, ImGridEntry *entry,
                         bool trigger_add_event, ImGridEntry *after) {

  if (!(entry->Position.x < 119 && entry->Position.y < 119)) {
    IM_ASSERT(false);
  }
  // determine if we have already added this node?

  ctx.InColumnResize ? (void)GridNodeBoundFix(ctx, entry)
                     : (void)GridPrepareEntry(ctx, entry);

  bool skip_collision = false;
  if (entry->AutoPosition &&
      GridFindEmptyPosition(ctx, *entry, ctx.Column, ctx.Entries, after)) {
    entry->AutoPosition = false;
    skip_collision = true;
  }

  ctx.Entries.push_back(entry);
  if (trigger_add_event)
    ctx.AddedEntries.push_back(entry);

  if (!skip_collision)
    GridFixCollisions(ctx, entry, entry->Position);
  if (!ctx.BatchMode)
    GridPackEntries(ctx);
  return entry;
}

void GridRemoveEntry(ImGridEngine &ctx, ImGridEntry *entry,
                     bool trigger_event) {

  if (!(entry->Position.x < 119 && entry->Position.y < 119)) {
    IM_ASSERT(false);
  }
  bool found = false;
  for (int i = 0; i < ctx.Entries.size(); i++) {
    if (ctx.Entries[i]->Id == entry->Id)
      found = true;
  }

  if (!found)
    return;

  if (trigger_event)
    ctx.RemovedEntries.push_back(entry);

  // TODO:
  //  don't use 'faster' .splice(findIndex(),1) in case node isn't in our
  //  list, or in multiple times.
  // this.nodes = this.nodes.filter(n => n._id !== node._id);
  // if (!node._isAboutToRemove) this._packNodes(); // if dragged out, no
  // need to relayout as already done... this._notify([node]);
}

/*
*   static sort(nodes: GridStackNode[], dir: 1 | -1 = 1): GridStackNode[] {
    const und = 10000;
    return nodes.sort((a, b) => {
      let diffY = dir * ((a.y ?? und) - (b.y ?? und));
      if (diffY === 0) return dir * ((a.x ?? und) - (b.x ?? und));
      return diffY;
    });
  }
*/

// TODO: does this need to be by ref?
bool GridChangedPosConstrain(ImGridEntry *entry, ImGridPosition &p) {
  if (p.w == -1)
    p.w = entry->Position.w;
  if (p.h == -1)
    p.h = entry->Position.h;
  if (entry->Position.x != p.x || entry->Position.y != p.y)
    return true;

  // check constrained w,h
  if (entry->MaxW) {
    p.w = IM_MIN(p.w, entry->MaxW);
  }
  if (entry->MaxH) {
    p.h = IM_MIN(p.h, entry->MaxH);
  }
  if (entry->MinW) {
    p.w = IM_MAX(p.w, entry->MinW);
  }
  if (entry->MinH) {
    p.h = IM_MAX(p.h, entry->MinH);
  }
  return (entry->Position.w != p.w || entry->Position.h != p.h);
}

int GridGetRow(ImGridEngine &ctx) {
  int max_row = 0;
  for (auto &entry : ctx.Entries) {
    max_row = IM_MAX(max_row, entry->Position.y + entry->Position.h);
  }
  return max_row;
}

bool GridEntryMoveCheck(ImGridEngine &ctx, ImGridEntry *entry,
                        ImGridMoveOptions opts) {
  if (!GridChangedPosConstrain(entry, opts.Position))
    return false;
  opts.Pack = true;

  if (ctx.MaxRow <= 0)
    return GridMoveNode(ctx, entry, opts);

  ImGridEntry *cloned_node = NULL;
  ImVector<ImGridEntry *> cloned_nodes;
  for (auto &node : ctx.Entries) {
    if (node->Id == entry->Id)
      cloned_node = node;

    cloned_nodes.push_back(node);
  }
  ImGridEngine dev_grid = ImGridEngine();
  dev_grid.Column = ctx.Column;
  dev_grid.MaxRow = 0;
  dev_grid.Entries = cloned_nodes;
  dev_grid.Float = ctx.Float;
  if (cloned_node == NULL)
    return false;

  bool can_move = GridMoveNode(dev_grid, cloned_node, opts) &&
                  GridGetRow(dev_grid) <= IM_MAX(GridGetRow(ctx), ctx.MaxRow);
  if (!can_move && !opts.Resizing && opts.Collide != NULL) {
    // TODO: check
    if (SwapEntryPositions(*entry, *opts.Collide))
      return true;
  }
  if (!can_move)
    return false;

  for (auto &node : dev_grid.Entries) {
    if (node->Dirty) {
      for (auto &n : ctx.Entries) {
        if (n->Id == node->Id) {
          GridCopyPosition(n, node);
          n->Dirty = true;
        }
      }
    }
  }
  return true;
}

void GridCleanNodes(ImGridEngine &ctx) {
  if (ctx.BatchMode)
    return;

  for (auto &entry : ctx.Entries) {
    entry->Dirty = false;
    entry->LastTried.Reset();
  }
}

void GridSaveInitial(ImGridEngine &ctx) {
  ctx.HasLocked = false;
  for (auto &entry : ctx.Entries) {
    entry->PrevPosition = entry->Position;
    entry->Dirty = false;
    ctx.HasLocked |= entry->Locked;
  }
}

void GridBatchUpdate(ImGridEngine &ctx, bool flag, bool do_pack) {
  if (ctx.BatchMode == flag)
    return;

  ctx.BatchMode = flag;
  if (flag) {
    ctx.PrevFloat = ctx.Float;
    ctx.Float = true;
    GridCleanNodes(ctx);
    GridSaveInitial(ctx);
  } else {
    ctx.Float = ctx.PrevFloat;
    if (do_pack)
      GridPackEntries(ctx);
  }
}

void GridCacheLayout(ImGridEngine &ctx, ImVector<ImGridEntry *> nodes,
                     int column, bool clear) {
  ImVector<ImGridEntry> entries;
  for (int i = 0; i < nodes.size(); ++i) {
    auto &node = nodes[i];
    // TODO: this is gross as we are only overwriting the h
    entries.push_back(ImGridEntry{
        ImGridPosition{
            node->Position.x,
            node->Position.y,
            node->Position.w,
            -1,
        },
    });
  }
  if (clear)
    ctx.Entries.clear();
  ctx.CacheLayouts[column] = entries;
}

void GridFindSpace(ImGridEngine &ctx, ImGridEntry *entry,
                   ImVector<ImGridEntry *> &node_list, int column,
                   ImGridEntry *after) {
  (void)ctx;
  float start = after != NULL ? after->Position.y * column +
                                    (after->Position.x + after->Position.w)
                              : 0;
  bool found = false;
  for (int i = start; !found; ++i) {
    int x = i % column;
    int y = IM_FLOOR(i / column);
    if (x + entry->Position.w > column)
      continue;

    ImGridPosition area = {static_cast<float>(x), static_cast<float>(y),
                           entry->Position.w, entry->Position.h};
    bool any_collision = false;
    for (auto &node : node_list) {
      if (GridPositionsAreIntercepted(node->Position, area)) {
        any_collision = true;
        break;
      }
    }
    if (!any_collision) {
      if (entry->Position.x != x || entry->Position.y != y)
        entry->Dirty = true;
      entry->Position.x = x;
      entry->Position.y = y;
      entry->AutoPosition = false;
      found = true;
    }
  }
}

void GridCompact(ImGridEngine &ctx, ImGridColumnFlags opts, bool do_sort) {
  if (ctx.Entries.size() == 0)
    return;

  if (do_sort)
    GridSortNodesInplace(ctx.Entries, true);

  const auto was_batch = ctx.BatchMode;
  if (!was_batch)
    GridBatchUpdate(ctx);

  const auto was_column_resize = ctx.InColumnResize;
  if (was_column_resize)
    ctx.InColumnResize = true;

  ImVector<ImGridEntry *> new_entries = ctx.Entries; // copy
  ctx.Entries.clear();

  for (int i = 0; i < new_entries.size(); ++i) {
    auto *n = new_entries[i];
    ImGridEntry *after = NULL;

    if (!n->Locked) {
      n->AutoPosition = true;
      if (opts & ImGridColumnFlags_List && i > 0) {
        after = new_entries[i - 1];
      }
    }

    GridAddNode(ctx, n, false, after);
  }

  if (!was_column_resize)
    ctx.InColumnResize = false;

  if (!was_batch)
    GridBatchUpdate(ctx, false, false);
}

void GridColumnChanged(ImGridEngine &ctx, int previous_column, int column,
                       ImGridColumnOptions opts) {
  if (ctx.Entries.size() == 0 || previous_column == column)
    return;

  if (opts.Flags == ImGridColumnFlags_None)
    return;

  bool compact = opts.Flags & ImGridColumnFlags_Compact ||
                 opts.Flags & ImGridColumnFlags_List;
  if (compact)
    GridSortNodesInplace(ctx.Entries, true);

  if (column < previous_column)
    GridCacheLayout(ctx, ctx.Entries, previous_column);
  GridBatchUpdate(ctx);

  ImVector<ImGridEntry *> new_entries;
  ImVector<ImGridEntry *> ordered_entries =
      compact ? ctx.Entries : GridSortNodes(ctx.Entries, false);
  if (column > previous_column) {
    int last_index = ctx.CacheLayouts.size() - 1;
    ImVector<ImGridEntry> &cache_nodes = ctx.CacheLayouts[last_index];
    if (!(cache_nodes.size() > 0) && previous_column != last_index &&
        ctx.CacheLayouts[last_index].size() > 0) {
      previous_column = last_index;
      for (auto &entry_wrapper : cache_nodes) {
        // find the matching entry in ordered_entries
        ImGridEntry *inner_entry = NULL;
        for (int node_ind = 0;
             node_ind < ordered_entries.size() && inner_entry == NULL;
             ++node_ind) {
          if (ordered_entries[node_ind]->Id == entry_wrapper.Id)
            inner_entry = ordered_entries[node_ind];
        }
        if (inner_entry != NULL) {
          if (!compact && !inner_entry->AutoPosition) {
            inner_entry->Position.x = entry_wrapper.Position.x;
            inner_entry->Position.y = entry_wrapper.Position.y;
          }
          inner_entry->Position.w = entry_wrapper.Position.w;
        }
      }
    }

    // new
    for (auto &cache_node : cache_nodes) {
      ImGridEntry *inner_entry = NULL;
      int found_index = -1;
      for (int node_ind = 0;
           node_ind < ordered_entries.size() && inner_entry == NULL;
           ++node_ind) {
        if (ordered_entries[node_ind]->Id == cache_node.Id)
          inner_entry = ordered_entries[node_ind];
      }
      if (inner_entry != NULL) {
        if (compact) {
          inner_entry->Position.w = cache_node.Position.w;
          continue;
        }

        if (cache_node.AutoPosition || cache_node.Position.x == -1 ||
            cache_node.Position.y == -1) {
          GridFindEmptyPosition(ctx, cache_node, ctx.Column, new_entries, NULL);
        }
        if (!cache_node.AutoPosition) {
          inner_entry->Position.x = cache_node.Position.x;
          inner_entry->Position.y = cache_node.Position.y;
          inner_entry->Position.w = cache_node.Position.w;
          new_entries.push_back(inner_entry);
        }
        // remove found_index from ordered_entries
        ordered_entries.erase(ordered_entries.begin() + found_index);
      }
    }
  }

  if (compact) {
    GridCompact(ctx, opts.Flags);
  } else {
    if (ordered_entries.size() > 0) {
      if (opts.Func != NULL) {
        opts.Func(column, previous_column, new_entries, ordered_entries);
      } else {
        float ratio = compact ? 1 : column / previous_column;
        bool move = (opts.Flags & ImGridColumnFlags_Move) ||
                    (opts.Flags & ImGridColumnFlags_MoveScale);
        bool scale = (opts.Flags & ImGridColumnFlags_Scale) ||
                     (opts.Flags & ImGridColumnFlags_MoveScale);
        for (int i = 0; i < ordered_entries.size(); ++i) {
          auto &entry = ordered_entries[i];
          entry->Position.x =
              (column == 1 ? 0
                           : (move ? IM_ROUND(entry->Position.x * ratio)
                                   : IM_MIN(entry->Position.x, column - 1)));
          entry->Position.w = ((column == 1 || previous_column == 1) ? 1
                               : scale ? (IM_ROUND(entry->Position.w * ratio))
                                       : IM_MIN(entry->Position.w, column));
          new_entries.push_back(entry);
        }
        ordered_entries.clear();
      }
    }

    GridSortNodesInplace(new_entries, false);
    ctx.InColumnResize = true;
    ctx.Entries.clear();
    for (int i = 0; i < new_entries.size(); ++i) {
      GridAddNode(ctx, new_entries[i], false);
      new_entries[i]->PrevPosition.Reset();
    }
  }

  for (int i = 0; i < ctx.Entries.size(); ++i) {
    ctx.Entries[i]->PrevPosition.Reset();
  }
  GridBatchUpdate(ctx, false, !compact);
  ctx.InColumnResize = false;
}

void GridBeginUpdate(ImGridEngine &ctx, ImGridEntry *node) {
  if (!node->Updating) {
    node->Updating = true;
    node->SkipDown = false;
    if (!ctx.BatchMode)
      GridSaveInitial(ctx);
  }
}

void GridEndUpdate(ImGridEngine &ctx) {
  for (auto &entry : ctx.Entries) {
    if (entry->Updating) {
      entry->Updating = false;
      entry->SkipDown = false;
    }
  }
}

} // namespace ImGrid::Engine
