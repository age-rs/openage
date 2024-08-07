// Copyright 2024-2024 the openage authors. See copying.md for legal info.

#pragma once

#include <cstddef>
#include <memory>
#include <unordered_set>
#include <vector>

#include "pathfinding/definitions.h"
#include "pathfinding/types.h"


namespace openage {
namespace coord {
struct tile;
} // namespace coord

namespace path {
class IntegrationField;
class Portal;

class FlowField {
public:
	/**
	 * Create a square flow field with a specified size.
	 *
	 * @param size Side length of the field.
	 */
	FlowField(size_t size);

	/**
	 * Create a flow field from an existing integration field.
	 *
	 * @param integration_field Integration field.
	 */
	FlowField(const std::shared_ptr<IntegrationField> &integration_field);

	/**
	 * Get the size of the flow field.
	 *
	 * @return Size of the flow field.
	 */
	size_t get_size() const;

	/**
	 * Get the flow field value at a specified position.
	 *
	 * @param pos Coordinates of the cell.
	 *
	 * @return Flowfield value at the specified position.
	 */
	flow_t get_cell(const coord::tile &pos) const;

	/**
	 * Get the flow field direction at a specified position.
	 *
	 * @param pos Coordinates of the cell.
	 *
	 * @return Flowfield direction at the specified position.
	 */
	flow_dir_t get_dir(const coord::tile &pos) const;

	/**
	 * Build the flow field.
	 *
	 * @param integration_field Integration field.
	 * @param target_cells Target cells of the flow field. These cells are ignored
	 *                     when building the field.
	 */
	void build(const std::shared_ptr<IntegrationField> &integration_field,
	           const std::unordered_set<size_t> &target_cells = {});

	/**
	 * Build the flow field for a portal.
	 *
	 * @param integration_field Integration field.
	 * @param other Integration field of the other sector.
	 * @param other_sector_id Sector ID of the other field.
	 * @param portal Portal connecting the two fields.
	 */
	void build(const std::shared_ptr<IntegrationField> &integration_field,
	           const std::shared_ptr<IntegrationField> &other,
	           sector_id_t other_sector_id,
	           const std::shared_ptr<Portal> &portal);

	/**
	 * Get the flow field values.
	 *
	 * @return Flow field values.
	 */
	const std::vector<flow_t> &get_cells() const;

	/**
	 * Reset the flow field values for rebuilding the field.
	 */
	void reset();

private:
	/**
	 * Side length of the field.
	 */
	size_t size;

	/**
	 * Flow field cells.
	 */
	std::vector<flow_t> cells;
};

} // namespace path
} // namespace openage
