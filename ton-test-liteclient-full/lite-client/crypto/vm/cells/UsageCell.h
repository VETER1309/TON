#pragma once
#include "vm/cells/Cell.h"
#include "vm/cells/CellUsageTree.h"

namespace vm {
class UsageCell : public Cell {
 private:
  struct PrivateTag {};

 public:
  UsageCell(Ref<Cell> cell, CellUsageTree::NodePtr tree_node, PrivateTag)
      : cell_(std::move(cell)), tree_node_(std::move(tree_node)) {
  }
  static Ref<Cell> create(Ref<Cell> cell, CellUsageTree::NodePtr tree_node) {
    if (tree_node.empty()) {
      return cell;
    }
    return Ref<UsageCell>{true, std::move(cell), std::move(tree_node), PrivateTag{}};
  }

  // load interface
  td::Result<LoadedCell> load_cell() const override {
    TRY_RESULT(loaded_cell, cell_->load_cell());
    if (tree_node_.on_load()) {
      CHECK(loaded_cell.tree_node.empty());
      loaded_cell.tree_node = tree_node_;
    }
    return std::move(loaded_cell);
  }
  Ref<Cell> virtualize(VirtualizationParameters virt) const override {
    auto virtualized_cell = cell_->virtualize(virt);
    if (tree_node_.empty()) {
      return virtualized_cell;
    }
    if (virtualized_cell.get() == cell_.get()) {
      return Ref<Cell>(this);
    }
    return create(std::move(virtualized_cell), tree_node_);
  }

  td::uint32 get_virtualization() const override {
    return cell_->get_virtualization();
  }

  bool is_loaded() const override {
    return cell_->is_loaded();
  }

  // hash and level
  LevelMask get_level_mask() const override {
    return cell_->get_level_mask();
  }

 protected:
  const Hash do_get_hash(td::uint32 level) const override {
    return cell_->get_hash(level);
  }
  td::uint16 do_get_depth(td::uint32 level) const override {
    return cell_->get_depth(level);
  }

 private:
  Ref<Cell> cell_;
  CellUsageTree::NodePtr tree_node_;
};
}  // namespace vm
