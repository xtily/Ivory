#include "Wallcheck.h"

#include <iostream>
#include <vector>
#include <mutex>
#include <cmath>

#include <sdk/game/game.h>
#include <core/scanner/rescan.h>
#include <sdk/cache/core/cache.h>

void c_wallcheck::find_valid_parts(const std::vector<rbx::c_instance>& instances, std::vector<rbx::c_primitive>& valid, std::vector<rbx::obb>& out_obstacles, std::int32_t depth) {
	if (depth > 6) return;
	for (rbx::c_instance child : instances) {
		if (!child.address || child.address < 0x10000 || child.address >= 0x7FFFFFFFFFFFull) continue;
		std::string className = child.get_class_name();

		if (
			className == "BasePart" ||
			className == "Part" ||
			className == "MeshPart" ||
			className == "WedgePart" ||
			className == "CornerWedgePart" ||
			className == "Ball" ||
			className == "Cylinder" ||
			className == "UnionOperation" ||
			className == "TrussPart"
			) {
			rbx::c_primitive prim = child.get_primitive();
			if (!prim.address || prim.address < 0x10000 || prim.address >= 0x7FFFFFFFFFFFull) continue;
			valid.push_back(prim);

			math::vector3 center = prim.get_position();
			math::vector3 size = prim.get_size();
			math::cframe cf = prim.get_cframe();

			if (size.x > 200.f || size.y > 200.f || size.z > 200.f ||
				size.x < 1.f || size.y < 1.f || size.z < 1.f) {
				continue;
			}

			rbx::obb obb(center, size, cf);
			out_obstacles.push_back(obb);
		}
		else if (className == "Folder" || className == "Model") {
			find_valid_parts(child.get_children<rbx::c_instance>(), valid, out_obstacles, depth + 1);
		}
	}
}

bool c_wallcheck::cache_workspace() {
	if (rescan::is_rescanning.load()) return false;

	std::uint64_t dm_addr = 0;
	{
		std::lock_guard<std::mutex> lock(cache::mtx);
		if (!game::datamodel || game::datamodel->address < 0x10000) return false;
		dm_addr = game::datamodel->address;
	}

	rbx::c_datamodel dm(dm_addr);
	rbx::c_instance workspace = dm.get_workspace();
	if (!workspace.address || workspace.address < 0x10000 || workspace.address >= 0x7FFFFFFFFFFFull) {
		return false;
	}

	std::vector<rbx::c_instance> children = workspace.get_children<rbx::c_instance>();
	std::vector<rbx::c_primitive> valid;
	std::vector<rbx::obb> new_obstacles;

	find_valid_parts(children, valid, new_obstacles, 0);

	if (valid.empty()) {
		return false;
	}

	{
		std::lock_guard<std::mutex> lock(mtx);
		parts = std::move(valid);
	}
	obstacles_snapshot.store(std::make_shared<const std::vector<rbx::obb>>(std::move(new_obstacles)));
	return true;
}

bool c_wallcheck::is_visible(const math::vector3& origin, const math::vector3& target) {
	if (rescan::is_rescanning.load()) return true;

	math::vector3 delta = target - origin;
	float dist_sq = delta.x * delta.x + delta.y * delta.y + delta.z * delta.z;
	if (dist_sq < 0.0001f) return true;

	float distance = std::sqrt(dist_sq);
	math::vector3 dir{ delta.x / distance, delta.y / distance, delta.z / distance };

	const auto obs = obstacles_snapshot.load();

	for (const rbx::obb& box : *obs) {
		float max_extent = (std::max)((std::max)(box.half_size.x, box.half_size.y), box.half_size.z);
		float max_reach = distance + max_extent;

		math::vector3 center_delta = box.center - origin;
		float center_dist_sq = center_delta.x * center_delta.x + center_delta.y * center_delta.y + center_delta.z * center_delta.z;
		if (center_dist_sq > max_reach * max_reach) continue;

		if (box.intersects(origin, dir, distance)) {
			return false;
		}
	}

	return true;
}

std::vector<rbx::c_primitive> c_wallcheck::get_parts() {
	std::lock_guard<std::mutex> lock(mtx);
	return parts;
}

std::vector<rbx::obb> c_wallcheck::get_obstacles() {
	auto obs = obstacles_snapshot.load();
	return *obs;
}