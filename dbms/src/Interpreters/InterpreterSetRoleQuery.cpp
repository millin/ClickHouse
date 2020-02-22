#include <Interpreters/InterpreterSetRoleQuery.h>
#include <Parsers/ASTSetRoleQuery.h>
#include <Parsers/ASTGenericRoleSet.h>
#include <Interpreters/Context.h>
#include <Access/GenericRoleSet.h>
#include <Access/AccessControlManager.h>
#include <Access/User.h>


namespace DB
{
namespace ErrorCodes
{
    extern const int SET_NON_GRANTED_ROLE;
}


BlockIO InterpreterSetRoleQuery::execute()
{
    const auto & query = query_ptr->as<const ASTSetRoleQuery &>();
    if (query.kind == ASTSetRoleQuery::Kind::SET_DEFAULT_ROLE)
        setDefaultRole(query);
    else
        setRole(query);
    return {};
}


void InterpreterSetRoleQuery::setRole(const ASTSetRoleQuery & query)
{
    auto & access_control = context.getAccessControlManager();
    auto & session_context = context.getSessionContext();
    auto user = session_context.getUser();

    if (query.kind == ASTSetRoleQuery::Kind::SET_ROLE_DEFAULT)
    {
        session_context.setCurrentRolesDefault();
    }
    else
    {
        GenericRoleSet roles_from_query{*query.roles, access_control};
        std::vector<UUID> new_current_roles;
        if (roles_from_query.all)
        {
            for (const auto & id : user->granted_roles)
                if (roles_from_query.match(id))
                    new_current_roles.push_back(id);
        }
        else
        {
            for (const auto & id : roles_from_query.getMatchingIDs())
            {
                if (!user->granted_roles.contains(id))
                    throw Exception("Role should be granted to set current", ErrorCodes::SET_NON_GRANTED_ROLE);
                new_current_roles.push_back(id);
            }
        }
        session_context.setCurrentRoles(new_current_roles);
    }
}


void InterpreterSetRoleQuery::setDefaultRole(const ASTSetRoleQuery & query)
{
    context.checkAccess(AccessType::CREATE_USER | AccessType::DROP_USER);

    auto & access_control = context.getAccessControlManager();
    std::vector<UUID> to_users = GenericRoleSet{*query.to_users, access_control, context.getUserID()}.getMatchingUsers(access_control);
    GenericRoleSet roles_from_query{*query.roles, access_control};

    auto update_func = [&](const AccessEntityPtr & entity) -> AccessEntityPtr
    {
        auto updated_user = typeid_cast<std::shared_ptr<User>>(entity->clone());
        updateUserSetDefaultRoles(*updated_user, roles_from_query);
        return updated_user;
    };

    access_control.update(to_users, update_func);
}


void InterpreterSetRoleQuery::updateUserSetDefaultRoles(User & user, const GenericRoleSet & roles_from_query)
{
    if (!roles_from_query.all)
    {
        for (const auto & id : roles_from_query.getMatchingIDs())
        {
            if (!user.granted_roles.contains(id))
                throw Exception("Role should be granted to set default", ErrorCodes::SET_NON_GRANTED_ROLE);
        }
    }
    user.default_roles = roles_from_query;
}

}