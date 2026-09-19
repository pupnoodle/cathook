#include "common.hpp"
#include "sdk/dt_recv_redef.h"
#include <cstddef>

static_assert(offsetof(RecvPropRedef, m_ProxyFn) == 0x30, "RecvProp::m_ProxyFn");
static_assert(offsetof(RecvPropRedef, m_Offset) == 0x48, "RecvProp::m_Offset");
static_assert(offsetof(RecvTable, m_nProps) == 8, "RecvTable::m_nProps");
static_assert(offsetof(RecvTable, m_pNetTableName) == 24, "RecvTable::m_pNetTableName");

/**
 * netvar_tree - Constructor
 *
 * Call populate_nodes on every RecvTable under client->GetAllClasses()
 */
void netvar_tree::init()
{
    const auto *client_class = g_IBaseClient->GetAllClasses();
    while (client_class != nullptr)
    {
        const auto class_info = std::make_shared<node>(0, nullptr);
        auto *recv_table      = client_class->m_pRecvTable;
        populate_nodes(recv_table, &class_info->nodes);
        nodes.emplace(recv_table->GetName(), class_info);
        client_class = client_class->m_pNext;
    }
}

/**
 * populate_nodes - Populate a node map with brances
 * @recv_table:	Table the map corresponds to
 * @map:	Map pointer
 *
 * Add info for every prop in the recv table to the node map. If a prop is a
 * datatable itself, initiate a recursive call to create more branches.
 */
void netvar_tree::populate_nodes(RecvTable *recv_table, map_type *map)
{
    map->clear();
    for (auto i = 0; i < recv_table->GetNumProps(); i++)
    {
        auto *prop               = reinterpret_cast<RecvPropRedef *>(recv_table->GetProp(i));
        const auto prop_info     = std::make_shared<node>(prop->m_Offset, reinterpret_cast<RecvProp *>(prop));
        if (prop->m_RecvType == DPT_DataTable && prop->m_pDataTable)
            populate_nodes(prop->m_pDataTable, &prop_info->nodes);
        map->emplace(prop->m_pVarName, prop_info);
    }
}

netvar_tree gNetvars;
