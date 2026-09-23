/*
 * CTFInventoryManager.cpp
 *
 *  Created on: Apr 26, 2018
 *      Author: bencat07
 */
#include "common.hpp"
#include "e8call.hpp"
#include "DetourHook.hpp"
#include <cstddef>
#include <unordered_set>
using namespace re;

struct inventory_layout
{
    int count_off{ 0x70 };
    int array_off{ 0x60 };
    int stride{ 0x150 };
    int def_off{ 0x44 };
    int id_high{ 0x58 };
    int id_low{ 0x5C };
};

static inventory_layout inv_layout()
{
    static inventory_layout layout = [] {
        inventory_layout out;
        auto *by_id = reinterpret_cast<uint8_t *>(gSignatures.GetClientSignature(sigs::inventory_find_item_by_id));
        if (by_id)
        {
            out.count_off = by_id[3];
            for (int i = 0; i < 48; ++i)
            {
                if (by_id[i] == 0x48 && by_id[i + 1] == 0x8B && by_id[i + 2] == 0x47)
                    out.array_off = by_id[i + 3];
                if (by_id[i] == 0x48 && by_id[i + 1] == 0x81 && by_id[i + 2] == 0xC6)
                    out.stride = *reinterpret_cast<int *>(by_id + i + 3);
                if (by_id[i] == 0x8B && by_id[i + 1] == 0x50)
                    out.id_high = by_id[i + 2];
                if (by_id[i] == 0x8B && by_id[i + 1] == 0x78)
                    out.id_low = by_id[i + 2];
            }
        }
        auto *by_def = reinterpret_cast<uint8_t *>(gSignatures.GetClientSignature(sigs::inventory_find_item_by_def));
        if (by_def)
        {
            for (int i = 0; i < 96; ++i)
            {
                if (by_def[i] == 0x0F && by_def[i + 1] == 0xB7 && by_def[i + 2] == 0x47)
                {
                    out.def_off = by_def[i + 3];
                    break;
                }
            }
        }
        logging::Info("CTFPlayerInventory layout count=%x array=%x stride=%x def=%x id=%x/%x", out.count_off, out.array_off, out.stride, out.def_off, out.id_high, out.id_low);
        return out;
    }();
    return layout;
}

CTFInventoryManager *CTFInventoryManager::GTFInventoryManager()
{
    auto *insn = reinterpret_cast<uint8_t *>(gSignatures.GetClientSignature(sigs::tf_inventory_manager_initializer));
    if (!insn)
        return nullptr;
    return reinterpret_cast<CTFInventoryManager *>(cathook::core::memory::resolve_rip_relative(insn + 1, 3, 7));
}
bool CTFInventoryManager::EquipItemInLoadout(int classid, int slot, unsigned long long uniqueid)
{
    typedef bool (*fn_t)(void *, int, int, unsigned long long);
    return vfunc<fn_t>(this, vtables::inventory::equip_item_in_loadout, 0)(this, classid, slot, uniqueid);
}

CTFPlayerInventory *CTFInventoryManager::GTFPlayerInventory()
{
    typedef CTFPlayerInventory *(*fn_t)(void *);
    return vfunc<fn_t>(this, vtables::inventory::get_tf_player_inventory, 0)(this);
}

int CTFPlayerInventory::GetItemCount()
{
    return *(int *) ((char *) this + inv_layout().count_off);
}

CEconItemView *CTFPlayerInventory::GetItem(int idx)
{
    const auto layout           = inv_layout();
    uintptr_t item_start        = *(uintptr_t *) ((char *) this + layout.array_off);
    return (CEconItemView *) (item_start + idx * layout.stride);
}

std::vector<unsigned long long> CTFPlayerInventory::GetItemsOfItemDef(int id)
{
    std::vector<unsigned long long> uuid_vec;
    for (int i = 0; i < this->GetItemCount(); i++)
    {
        auto item = this->GetItem(i);
        if (item->GetDefinitionIndex() == id)
            uuid_vec.push_back(item->UUID());
    }
    return uuid_vec;
}

CEconItemView *CTFPlayerInventory::GetFirstItemOfItemDef(int id)
{
    typedef CEconItemView *(*GetFirstItemOfItemDef_t)(void *, int16_t);
    static uintptr_t address                                = gSignatures.GetClientSignature(sigs::inventory_find_item_by_def);
    static GetFirstItemOfItemDef_t GetFirstItemOfItemDef_fn = GetFirstItemOfItemDef_t(address);
    return GetFirstItemOfItemDef_fn ? GetFirstItemOfItemDef_fn(this, int16_t(id)) : nullptr;
}

int CEconItemView::GetDefinitionIndex()
{
    return *(ushort *) ((uintptr_t) this + inv_layout().def_off);
}

unsigned long long CEconItemView::UUID()
{
    const auto layout = inv_layout();
    auto high         = *(unsigned int *) ((char *) this + layout.id_high);
    auto low          = *(unsigned int *) ((char *) this + layout.id_low);
    return (unsigned long long) high << 32 | low;
}

static CatCommand equip_debug("equip_debug", "Debug auto equip stuff", []() {
    auto invmng    = CTFInventoryManager::GTFInventoryManager();
    auto inv       = invmng->GTFPlayerInventory();
    auto item_view = inv->GetFirstItemOfItemDef(56);
    if (item_view)
    {
        logging::Info("%llu", item_view->UUID());
        logging::Info("Equip item: %d", invmng->EquipItemInLoadout(tf_sniper, 0, item_view->UUID()));
    }
});

static CatCommand list_debug("equip_list_debug", "Debug item def listing", []() {
    auto invmng = CTFInventoryManager::GTFInventoryManager();
    auto inv    = invmng->GTFPlayerInventory();
    auto items  = inv->GetItemsOfItemDef(5000);
    for (auto item : items)
        logging::Info("%llu", item);
});

struct craft_layout
{
    int items{ int(vtables::crafting_panel::input_items) };
    int items_end{ int(vtables::crafting_panel::input_items_end) };
    int recipe{ int(vtables::crafting_panel::selected_recipe) };
};

static craft_layout live_craft_layout(uint8_t *craft)
{
    craft_layout out;
    if (!craft)
        return out;
    if (craft[1] == 0x48 && craft[2] == 0x8D && craft[3] == 0x87)
        out.items_end = *reinterpret_cast<int *>(craft + 4);
    for (int i = 0; i < 48; ++i)
    {
        if (craft[i] == 0x48 && craft[i + 1] == 0x8D && craft[i + 2] == 0x9F)
        {
            out.items = *reinterpret_cast<int *>(craft + i + 3);
            break;
        }
    }
    std::unordered_set<int> incremented;
    for (int i = 0; i + 6 < 0x280; ++i)
    {
        if (craft[i] == 0x83 && (craft[i + 1] & 0xC7) == 0x87 && craft[i + 6] == 0x01)
            incremented.insert(*reinterpret_cast<const int *>(craft + i + 2));
    }
    for (int i = 0; i + 5 < 0x280; ++i)
    {
        if (craft[i] == 0x8B && (craft[i + 1] & 0xC7) == 0x87)
        {
            int disp = *reinterpret_cast<int *>(craft + i + 2);
            if (disp > out.items_end && disp < out.items_end + 0x200 && !incremented.count(disp))
            {
                out.recipe = disp;
                break;
            }
        }
    }
    logging::Info("CCraftingPanel layout items=%x end=%x recipe=%x", out.items, out.items_end, out.recipe);
    return out;
}

static uintptr_t craft_getvpanel(void *)
{
    return 0;
}

bool Craft(std::vector<int> item_ids)
{
    static auto craft_func = gSignatures.GetClientSignature(sigs::crafting_panel_craft);
    if (!craft_func)
        return false;

    static const auto layout = live_craft_layout(reinterpret_cast<uint8_t *>(craft_func));
    static void *craft_vt[]  = { reinterpret_cast<void *>(craft_getvpanel) };

    auto invmng = CTFInventoryManager::GTFInventoryManager();
    if (!invmng)
        return false;
    auto inv = invmng->GTFPlayerInventory();
    if (!inv)
        return false;

    std::vector<unsigned long long> item_UUIDs;
    for (auto &id : item_ids)
    {
        auto items = inv->GetItemsOfItemDef(id);
        for (auto &item : items)
        {
            bool failed = false;
            for (auto &uuid : item_UUIDs)
                if (uuid == item)
                    failed = true;
            if (failed)
                continue;
            item_UUIDs.push_back(item);
            break;
        }
    }
    if (item_UUIDs.empty())
        return false;

    alignas(16) char panel[0x800]{};
    *reinterpret_cast<void **>(panel) = craft_vt;
    const int slots                   = (layout.items_end - layout.items) / int(sizeof(unsigned long long));
    const int n                       = std::min(int(item_UUIDs.size()), std::max(slots, 1));
    for (int i = 0; i < n; i++)
        *reinterpret_cast<unsigned long long *>(panel + layout.items + i * int(sizeof(unsigned long long))) = item_UUIDs[i];
    *reinterpret_cast<int *>(panel + layout.recipe) = -2;
    typedef void (*Craft_t)(void *);
    ((Craft_t) craft_func)(panel);
    return true;
}

bool Rent(int item_id)
{
    typedef void (*DoPreviewItem_t)(void *, int);
    static auto DoPreviewItem_addr = gSignatures.GetClientSignature(sigs::store_do_preview_item);
    if (!DoPreviewItem_addr)
        return false;
    auto DoPreviewItem_fn = (DoPreviewItem_t) DoPreviewItem_addr;
    if (!DoPreviewItem_fn)
        return false;
    DoPreviewItem_fn(nullptr, item_id);
    return true;
}

static CatCommand debug_panel("equip_debug_panel", "Debug the Crafting panel that gets used", []() { Craft({ 5000, 5000, 5000 }); });

