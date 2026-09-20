#pragma once

#include <limits>
#include <menu/interface/IMessageHandler.hpp>
#include <settings/Settings.hpp>

namespace hacks::tf2::skinchanger
{
struct SkinConfig;
}

namespace zerokernel
{
class Container;
class Text;
class Box;
class ScrollableList;
} // namespace zerokernel

namespace zerokernel::special
{

class SkinChangerController : public IMessageHandler
{
public:
    explicit SkinChangerController(Container &list);

    void update();

    void handleMessage(Message &msg, bool is_relayed) override;

private:
    int editingKey();
    hacks::tf2::skinchanger::SkinConfig varsToSkin();
    void commitSkin(hacks::tf2::skinchanger::SkinConfig skin);
    void rebuildKitList(int key);
    void syncVars(int key);
    void installCallbacks();

    Container &list;

    settings::Variable<int> weapon{};
    settings::Variable<int> kit{};
    settings::Variable<float> wear{};
    settings::Variable<int> seed{};
    settings::Variable<int> quality{};
    settings::Variable<bool> festive{};
    settings::Variable<bool> australium{};
    settings::Variable<int> killstreak{};
    settings::Variable<int> sheen{};
    settings::Variable<int> unusual{};

    Text *status{ nullptr };
    Box *kit_box{ nullptr };
    ScrollableList *kit_list{ nullptr };
    int last_key{ std::numeric_limits<int>::min() };
    bool syncing{ false };
    bool sync_needed{ true };
};
} // namespace zerokernel::special
