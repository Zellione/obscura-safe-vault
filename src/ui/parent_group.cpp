#include "ui/parent_group.h"

#include <algorithm>

namespace ui {

std::vector<ParentGroup> group_by_parent(std::span<const std::string> full_paths)
{
    std::vector<ParentGroup> groups;
    for (const std::string& path : full_paths) {
        const auto  slash  = path.find_last_of('/');
        const auto  parent = (slash == std::string::npos) ? std::string{}
                                                          : path.substr(0, slash);
        const auto  name   = (slash == std::string::npos) ? path
                                                          : path.substr(slash + 1);
        if (name.empty()) { continue; }

        auto it = std::ranges::find_if(groups, [&](const ParentGroup& g) {
            return g.parent == parent;
        });
        if (it == groups.end()) {
            groups.push_back({.parent = parent, .names = {name}});
        } else {
            it->names.push_back(name);
        }
    }
    return groups;
}

} // namespace ui
