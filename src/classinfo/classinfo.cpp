#include "classinfo/dynamic.gen.hpp"
#include "core/logging.hpp"

void InitClassTable()
{
    client_classes::dynamic_list.Populate();
    logging::Info("Class IDs: CTFPlayer=%d CTFWearableDemoShield=%d", client_classes::dynamic_list.CTFPlayer, client_classes::dynamic_list.CTFWearableDemoShield);
}
