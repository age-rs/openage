// Copyright 2024-2024 the openage authors. See copying.md for legal info.

#include "pathfinder.h"

#include "coord/phys.h"
#include "pathfinding/flow_field.h"
#include "pathfinding/grid.h"
#include "pathfinding/integrator.h"
#include "pathfinding/portal.h"
#include "pathfinding/sector.h"


namespace openage::path {

Pathfinder::Pathfinder() :
	grids{},
	integrator{std::make_shared<Integrator>()} {
}

const Path Pathfinder::get_path(const PathRequest &request) {
	// High-level pathfinding
	// Find the portals to use to get from the start to the target
	auto portal_path = this->portal_a_star(request);

	// Low-level pathfinding
	// Find the path within the sectors
	auto grid = this->grids.at(request.grid_id);
	auto sector_size = grid->get_sector_size();

	auto target_sector_x = request.target.ne / sector_size;
	auto target_sector_y = request.target.se / sector_size;
	auto target_sector = grid->get_sector(target_sector_x, target_sector_y);

	coord::tile_t target_x = request.target.ne % sector_size;
	coord::tile_t target_y = request.target.se % sector_size;
	auto target = coord::tile{target_x, target_y};

	auto sector_fields = this->integrator->build(target_sector->get_cost_field(), target);
	auto prev_integration_field = sector_fields.first;
	auto prev_sector_id = target_sector->get_id();

	std::vector<std::pair<sector_id_t, std::shared_ptr<FlowField>>> flow_fields;
	flow_fields.reserve(portal_path.size() + 1);

	flow_fields.push_back(std::make_pair(target_sector->get_id(), sector_fields.second));
	for (auto &portal : portal_path) {
		auto prev_sector = grid->get_sector(prev_sector_id);
		auto next_sector_id = portal->get_exit_sector(prev_sector_id);
		auto next_sector = grid->get_sector(next_sector_id);

		sector_fields = this->integrator->build(next_sector->get_cost_field(),
		                                        prev_integration_field,
		                                        prev_sector_id,
		                                        portal);
		flow_fields.push_back(std::make_pair(next_sector_id, sector_fields.second));

		prev_integration_field = sector_fields.first;
		prev_sector_id = next_sector_id;
	}

	// reverse the flow fields so they are ordered from start to target
	std::reverse(flow_fields.begin(), flow_fields.end());

	// traverse the flow fields to get the waypoints
	std::vector<coord::tile> waypoints{request.start};
	auto flow_field_waypoints = this->get_waypoints(flow_fields, request);
	waypoints.insert(waypoints.end(), flow_field_waypoints.begin(), flow_field_waypoints.end());

	return Path{request.grid_id, waypoints};
}

const std::shared_ptr<Grid> &Pathfinder::get_grid(grid_id_t id) const {
	return this->grids.at(id);
}

void Pathfinder::add_grid(const std::shared_ptr<Grid> &grid) {
	this->grids[grid->get_id()] = grid;
}

const std::vector<std::shared_ptr<Portal>> Pathfinder::portal_a_star(const PathRequest &request) const {
	std::vector<std::shared_ptr<Portal>> result;

	auto grid = this->grids.at(request.grid_id);
	auto sector_size = grid->get_sector_size();

	auto start_sector_x = request.start.ne / sector_size;
	auto start_sector_y = request.start.se / sector_size;
	auto start_sector = grid->get_sector(start_sector_x, start_sector_y);

	auto target_sector_x = request.target.ne / sector_size;
	auto target_sector_y = request.target.se / sector_size;
	auto target_sector = grid->get_sector(target_sector_x, target_sector_y);

	if (start_sector == target_sector) {
		// exit early if the start and target are in the same sector
		return result;
	}

	// path node storage, always provides cheapest next node.
	heap_t node_candidates;

	// list of known portals and corresponding node.
	nodemap_t visited_portals;

	// Cost to travel from one portal to another
	// TODO: Determine this cost for each portal
	// const int distance_cost = 1;

	// start nodes: all portals in the start sector
	for (auto &portal : start_sector->get_portals()) {
		auto portal_node = std::make_shared<PortalNode>(portal, start_sector->get_id(), nullptr);

		auto sector_pos = grid->get_sector(portal->get_exit_sector(start_sector->get_id()))->get_position();
		auto portal_pos = portal->get_exit_center(start_sector->get_id());
		auto portal_abs_pos = portal_pos + coord::tile_delta(sector_pos.ne * sector_size, sector_pos.se * sector_size);
		auto heuristic_cost = Pathfinder::heuristic_cost(portal_abs_pos, request.target);

		portal_node->current_cost = 0;
		portal_node->heuristic_cost = heuristic_cost;
		portal_node->future_cost = portal_node->current_cost + heuristic_cost;

		portal_node->heap_node = node_candidates.push(portal_node);
		visited_portals[portal->get_id()] = portal_node;
	}

	// track the closest we can get to the end position
	// used when no path is found
	auto closest_node = node_candidates.top();

	// while there are candidates to visit
	while (not node_candidates.empty()) {
		auto current_node = node_candidates.pop();

		current_node->was_best = true;

		// check if the current node is the target
		if (current_node->portal->get_exit_sector(current_node->entry_sector) == target_sector->get_id()) {
			auto backtrace = current_node->generate_backtrace();
			for (auto &node : backtrace) {
				result.push_back(node->portal);
			}
			return result;
		}

		// check if the current node is the closest to the target
		if (current_node->heuristic_cost < closest_node->heuristic_cost) {
			closest_node = current_node;
		}

		// get the exits of the current node
		auto exits = current_node->get_exits(visited_portals, current_node->entry_sector);

		// evaluate all neighbors of the current candidate for further progress
		for (auto &exit : exits) {
			if (exit->was_best) {
				continue;
			}

			// Get distance cost from current node to exit
			auto distance_cost = Pathfinder::distance_cost(
				current_node->portal->get_exit_center(current_node->entry_sector),
				exit->portal->get_entry_center(exit->entry_sector));

			bool not_visited = !visited_portals.contains(exit->portal->get_id());
			auto tentative_cost = current_node->current_cost + distance_cost;

			if (not_visited or tentative_cost < exit->current_cost) {
				if (not_visited) {
					// calculate the heuristic cost
					exit->heuristic_cost = Pathfinder::heuristic_cost(
						exit->portal->get_exit_center(exit->entry_sector),
						request.target);
				}

				// update the cost knowledge
				exit->current_cost = tentative_cost;
				exit->future_cost = exit->current_cost + exit->heuristic_cost;
				exit->prev_portal = current_node;

				if (not_visited) {
					exit->heap_node = node_candidates.push(exit);
					visited_portals[exit->portal->get_id()] = exit;
				}
				else {
					node_candidates.decrease(exit->heap_node);
				}
			}
		}
	}

	// no path found, return the closest node
	auto backtrace = closest_node->generate_backtrace();
	for (auto &node : backtrace) {
		result.push_back(node->portal);
	}

	return result;
}

const std::vector<coord::tile> Pathfinder::get_waypoints(const std::vector<std::pair<sector_id_t, std::shared_ptr<FlowField>>> &flow_fields,
                                                         const PathRequest &request) const {
	ENSURE(flow_fields.size() > 0, "At least 1 flow field is required for finding waypoints.");

	std::vector<coord::tile> waypoints;

	auto grid = this->get_grid(request.grid_id);
	auto sector_size = grid->get_sector_size();
	coord::tile_t start_x = request.start.ne % sector_size;
	coord::tile_t start_y = request.start.se % sector_size;
	coord::tile_t target_x = request.target.ne % sector_size;
	coord::tile_t target_y = request.target.se % sector_size;

	coord::tile_t current_x = start_x;
	coord::tile_t current_y = start_y;
	flow_dir_t current_direction = flow_fields.at(0).second->get_dir(coord::tile{current_x, current_y});
	for (size_t i = 0; i < flow_fields.size(); ++i) {
		auto sector = grid->get_sector(flow_fields.at(i).first);
		auto flow_field = flow_fields.at(i).second;

		// navigate the flow field vectors until we reach its edge (or the target)
		while (current_x < static_cast<coord::tile_t>(sector_size)
		       and current_y < static_cast<coord::tile_t>(sector_size)
		       and current_x >= 0
		       and current_y >= 0) {
			auto cell = flow_field->get_cell(coord::tile{current_x, current_y});
			if (cell & FLOW_LOS_MASK) {
				// check if we reached an LOS cell
				auto sector_pos = sector->get_position();
				auto cell_pos = coord::tile(sector_pos.ne * sector_size,
				                            sector_pos.se * sector_size)
				                + coord::tile_delta(current_x, current_y);
				waypoints.push_back(cell_pos);
				break;
			}

			// check if we need to change direction
			auto cell_direction = flow_field->get_dir(coord::tile(current_x, current_y));
			if (cell_direction != current_direction) {
				// add the current cell as a waypoint
				auto sector_pos = sector->get_position();
				auto cell_pos = coord::tile(sector_pos.ne * sector_size,
				                            sector_pos.se * sector_size)
				                + coord::tile_delta(current_x, current_y);
				waypoints.push_back(cell_pos);
				current_direction = cell_direction;
			}

			// move to the next cell
			switch (current_direction) {
			case flow_dir_t::NORTH:
				current_y -= 1;
				break;
			case flow_dir_t::NORTH_EAST:
				current_x += 1;
				current_y -= 1;
				break;
			case flow_dir_t::EAST:
				current_x += 1;
				break;
			case flow_dir_t::SOUTH_EAST:
				current_x += 1;
				current_y += 1;
				break;
			case flow_dir_t::SOUTH:
				current_y += 1;
				break;
			case flow_dir_t::SOUTH_WEST:
				current_x -= 1;
				current_y += 1;
				break;
			case flow_dir_t::WEST:
				current_x -= 1;
				break;
			case flow_dir_t::NORTH_WEST:
				current_x -= 1;
				current_y -= 1;
				break;
			default:
				throw Error{ERR << "Invalid flow direction: " << static_cast<int>(current_direction)};
			}
		}

		// reset the current position for the next flow field
		switch (current_direction) {
		case flow_dir_t::NORTH:
			current_y = sector_size - 1;
			break;
		case flow_dir_t::NORTH_EAST:
			current_x = current_x + 1;
			current_y = sector_size - 1;
			break;
		case flow_dir_t::EAST:
			current_x = 0;
			break;
		case flow_dir_t::SOUTH_EAST:
			current_x = 0;
			current_y = current_y + 1;
			break;
		case flow_dir_t::SOUTH:
			current_y = 0;
			break;
		case flow_dir_t::SOUTH_WEST:
			current_x = current_x - 1;
			current_y = 0;
			break;
		case flow_dir_t::WEST:
			current_x = sector_size - 1;
			break;
		case flow_dir_t::NORTH_WEST:
			current_x = sector_size - 1;
			current_y = current_y - 1;
			break;
		default:
			throw Error{ERR << "Invalid flow direction: " << static_cast<int>(current_direction)};
		}
	}

	// add the target position as the last waypoint
	auto sector_pos = grid->get_sector(flow_fields.back().first)->get_position();
	auto target_pos = coord::tile(sector_pos.ne * sector_size, sector_pos.se * sector_size)
	                  + coord::tile_delta{target_x, target_y};
	waypoints.push_back(target_pos);

	return waypoints;
}

int Pathfinder::heuristic_cost(const coord::tile &portal_pos,
                               const coord::tile &target_pos) {
	auto portal_phys_pos = portal_pos.to_phys2();
	auto target_phys_pos = target_pos.to_phys2();
	auto delta = target_phys_pos - portal_phys_pos;

	return delta.length();
}

int Pathfinder::distance_cost(const coord::tile &portal1_pos,
                              const coord::tile &portal2_pos) {
	auto delta = portal2_pos.to_phys2() - portal1_pos.to_phys2();

	return delta.length();
}


PortalNode::PortalNode(const std::shared_ptr<Portal> &portal,
                       sector_id_t entry_sector,
                       const node_t &prev_portal) :
	portal{portal},
	entry_sector{entry_sector},
	future_cost{std::numeric_limits<int>::max()},
	current_cost{std::numeric_limits<int>::max()},
	heuristic_cost{std::numeric_limits<int>::max()},
	was_best{false},
	prev_portal{prev_portal},
	heap_node{nullptr} {}

PortalNode::PortalNode(const std::shared_ptr<Portal> &portal,
                       sector_id_t entry_sector,
                       const node_t &prev_portal,
                       int past_cost,
                       int heuristic_cost) :
	portal{portal},
	entry_sector{entry_sector},
	future_cost{past_cost + heuristic_cost},
	current_cost{past_cost},
	heuristic_cost{heuristic_cost},
	was_best{false},
	prev_portal{prev_portal},
	heap_node{nullptr} {
}

bool PortalNode::operator<(const PortalNode &other) const {
	return this->future_cost < other.future_cost;
}

bool PortalNode::operator==(const PortalNode &other) const {
	return this->portal->get_id() == other.portal->get_id();
}

std::vector<node_t> PortalNode::generate_backtrace() {
	std::vector<node_t> waypoints;

	node_t current = this->shared_from_this();
	do {
		waypoints.push_back(current);
		current = current->prev_portal;
	}
	while (current != nullptr);

	return waypoints;
}

std::vector<node_t> PortalNode::get_exits(const nodemap_t &nodes,
                                          sector_id_t entry_sector) {
	std::vector<node_t> exits;

	auto exit_sector = this->portal->get_exit_sector(entry_sector);
	for (auto &exit : this->portal->get_exits(entry_sector)) {
		auto exit_id = exit->get_id();

		if (nodes.contains(exit_id)) {
			exits.push_back(nodes.at(exit_id));
		}
		else {
			exits.push_back(std::make_shared<PortalNode>(exit,
			                                             exit_sector,
			                                             this->shared_from_this()));
		}
	}
	return exits;
}


bool compare_node_cost::operator()(const node_t &lhs, const node_t &rhs) const {
	return *lhs < *rhs;
}

} // namespace openage::path
