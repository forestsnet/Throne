#pragma once
#include "include/global/Configs.hpp"
#include <QList>
namespace Configs {
struct Group { QString name, url; int id = 1; };
struct GroupsRepo {
    QList<int> GetGroupsTabOrder() { return {}; }
    std::shared_ptr<Group> GetGroup(int) { return {}; }
    static std::shared_ptr<Group> NewGroup() { return std::make_shared<Group>(); }
    bool AddGroup(const std::shared_ptr<Group> &) { return true; }
};
}
