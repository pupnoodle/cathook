/*
 * CTFInventoryManager.cpp
 *
 *  Created on: Apr 26, 2018
 *      Author: bencat07
 */
#include "common.hpp"
#include "e8call.hpp"
#include "DetourHook.hpp"
using namespace re;

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
    return vfunc<fn_t>(this, offsets::PlatformOffset(20, offsets::undefined, 20), 0)(this, classid, slot, uniqueid);
}
unsigned long long int CEconItem::uniqueid()
{
    return *((unsigned long long int *) this + 36);
}
CTFPlayerInventory *CTFInventoryManager::GTFPlayerInventory()
{
    typedef CTFPlayerInventory *(*fn_t)(void *);
    return vfunc<fn_t>(this, 24, 0)(this);
}

int CTFPlayerInventory::GetItemCount()
{
    return *(int *) ((char *) this + 112);
}

#define SIZE_OF_ITEMVIEW 336
CEconItemView *CTFPlayerInventory::GetItem(int idx)
{
    uintptr_t item_start  = *(uintptr_t *) ((char *) this + 96);
    uintptr_t item_offset = idx * SIZE_OF_ITEMVIEW;
    return (CEconItemView *) (item_start + item_offset);
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
    return *(ushort *) ((uintptr_t) this + 0x44);
}

unsigned long long CEconItemView::UUID()
{
    auto high = *(unsigned int *) ((char *) this + 88);
    auto low  = *(unsigned int *) ((char *) this + 92);
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

// Crafting slots on crafting page
#define CRAFTING_SLOTS_INPUT_ROWS 3
#define CRAFTING_SLOTS_INPUT_COLUMNS 4
#define CRAFTING_SLOTS_INPUTPANELS (CRAFTING_SLOTS_INPUT_ROWS * CRAFTING_SLOTS_INPUT_COLUMNS)

class CCraftingPanel_reclass2
{
public:
    char unknown_data[560];
    // Items in the input model panels
    unsigned long long m_InputItems[CRAFTING_SLOTS_INPUTPANELS];
    const int *m_ItemPanelCriteria[CRAFTING_SLOTS_INPUTPANELS];

    char unknown_data2[72];
    int m_iCurrentlySelectedRecipe;

    char unknown_data3[50];

    int m_iCraftingAttempts;
};

// Pass the Definition Indexes of the items you want to craft with (https://wiki.alliedmods.net/Team_Fortress_2_Item_Definition_Indexes)
bool Craft(std::vector<int> item_ids)
{
    static auto craft_func = uintptr_t(0);

    if (!craft_func)
        return false;

    // Return early to avoid a crash
    static auto patch_addr = SigAdd(craft_func, 0x22e);
    static BytePatch patch(patch_addr, { 0x83, 0xc4, 0x6c, 0x5b, 0x5e, 0x5f, 0x5d, 0xc3 });
    patch.Patch();

    CCraftingPanel_reclass2 panel{};
    auto invmng = CTFInventoryManager::GTFInventoryManager();
    if (!invmng)
        return false;
    auto inv    = invmng->GTFPlayerInventory();

    std::vector<unsigned long long> item_UUIDs;

    // Iterate all desired items
    for (auto &id : item_ids)
    {
        auto items = inv->GetItemsOfItemDef(id);
        // Iterate all the items matching the ID
        for (auto &item : items)
        {
            // Only add entries which are not added yet
            bool failed = false;
            for (auto &uuid : item_UUIDs)
                if (uuid == item)
                    failed = true;

            // Continue to next entry on failure
            if (failed)
                continue;
            else
            {
                item_UUIDs.push_back(item);
                break;
            }
        }
    }

    // No items found
    if (item_UUIDs.empty())
        return false;

    // Add items
    for (int i = 0; i < item_UUIDs.size(); i++)
    {
        panel.m_InputItems[i] = item_UUIDs[i];
    }

    // Select "Custom" Recipe
    panel.m_iCurrentlySelectedRecipe = -2;

    // Call the Craft function
    typedef void (*Craft_t)(CCraftingPanel_reclass2 *);
    Craft_t Craft_fn = (Craft_t) craft_func;
    Craft_fn(&panel);

    // Undo our patches to the game
    patch.Shutdown();
    return true;
}

bool Rent(int item_id)
{
    typedef void (*DoPreviewItem_t)(void *, int);
    static auto DoPreviewItem_addr = uintptr_t(0);
    if (!DoPreviewItem_addr)
        return false;
    static auto id = SigAdd(DoPreviewItem_addr, 0xf3);

    // Request the item instead of testing if we can request it
    static BytePatch id_patch(id, { 0xa7 });
    id_patch.Patch();

    auto DoPreviewItem_fn = (DoPreviewItem_t) DoPreviewItem_addr;
    if (!DoPreviewItem_fn)
        return false;

    // The nullptr is "this" but it's unused in the function
    DoPreviewItem_fn(nullptr, item_id);

    // Reset the id
    id_patch.Shutdown();
    return true;
}

static CatCommand debug_panel("equip_debug_panel", "Debug the Crafting panel that gets used", []() { Craft({ 5000, 5000, 5000 }); });
