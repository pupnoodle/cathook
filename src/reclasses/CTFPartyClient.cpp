/*
 * CTFPartyClient.cpp
 *
 *  Created on: Dec 7, 2017
 *      Author: nullifiedcat
 */

#include "common.hpp"
#include "core/e8call.hpp"

re::CTFPartyClient *re::CTFPartyClient::GTFPartyClient()
{
    typedef re::CTFPartyClient *(*GTFPartyClient_t)(void);
    static uintptr_t addr                     = SigAdd(gSignatures.GetClientSignature(sigs::get_party_client), sigs::get_party_client_offset);
    static GTFPartyClient_t GTFPartyClient_fn = GTFPartyClient_t(addr);
    return GTFPartyClient_fn ? GTFPartyClient_fn() : nullptr;
}

bool re::CTFPartyClient::BInQueue(re::CTFPartyClient *this_)
{
    return this_ && (*(uint8_t *) ((uint8_t *) this_ + 0x68) || this_->BInQueueForMatchGroup(0));
}

int re::CTFPartyClient::GetNumOnlineMembers()
{
    typedef int (*GetNumOnlineMembers_t)(re::CTFPartyClient *);
    static auto fn = GetNumOnlineMembers_t(gSignatures.GetClientSignature(sigs::party_client_get_num_online_members));
    return fn ? fn(this) : 0;
}

int re::CTFPartyClient::GetNumMembers()
{
    typedef int (*GetNumMembers_t)(re::CTFPartyClient *);
    static auto fn = GetNumMembers_t(gSignatures.GetClientSignature(sigs::party_client_get_num_members));
    return fn ? fn(this) : 0;
}

void re::CTFPartyClient::SendPartyChat(const char *message)
{
    typedef void (*SendPartyChat_t)(re::CTFPartyClient *, const char *);
    static auto fn = SendPartyChat_t(gSignatures.GetClientSignature(sigs::party_client_send_party_chat));
    if (fn)
        fn(this, message);
}

bool re::CTFPartyClient::BCanQueueForStandby(re::CTFPartyClient *this_)
{
    return this_ && !this_->BInQueueForStandby();
}

re::ITFGroupMatchCriteria *re::CTFPartyClient::MutLocalGroupCriteria(re::CTFPartyClient *client)
{
    return client ? reinterpret_cast<re::ITFGroupMatchCriteria *>(uintptr_t(client) + 0x1B0) : nullptr;
}

int re::CTFPartyClient::LoadSavedCasualCriteria()
{
    typedef int (*LoadSavedCasualCriteria_t)(re::CTFPartyClient *);
    static auto fn = LoadSavedCasualCriteria_t(gSignatures.GetClientSignature(sigs::load_saved_casual_criteria));
    return fn ? fn(this) : 0;
}

void re::CTFPartyClient::RequestQueueForStandby()
{
    typedef void (*RequestStandby_t)(re::CTFPartyClient *);
    static auto fn = RequestStandby_t(gSignatures.GetClientSignature(sigs::request_queue_for_standby));
    if (fn)
        fn(this);
}

char re::CTFPartyClient::RequestQueueForMatch(int type)
{
    typedef char (*RequestQueueForMatch_t)(re::CTFPartyClient *, int);
    static auto fn = RequestQueueForMatch_t(gSignatures.GetClientSignature(sigs::request_queue_for_match));
    return fn ? fn(this, type) : 0;
}

bool re::CTFPartyClient::BInQueueForMatchGroup(int type)
{
    typedef bool (*BInQueueForMatchGroup_t)(re::CTFPartyClient *, int);
    static auto fn = BInQueueForMatchGroup_t(gSignatures.GetClientSignature(sigs::is_in_queue_for_match_group));
    return fn ? fn(this, type) : false;
}

bool re::CTFPartyClient::BInQueueForStandby()
{
    return *((unsigned char *) this + 0x68);
}

char re::CTFPartyClient::RequestLeaveForMatch(int type)
{
    typedef char (*RequestLeaveForMatch_t)(re::CTFPartyClient *, int);
    static auto fn = RequestLeaveForMatch_t(gSignatures.GetClientSignature(sigs::request_leave_for_match));
    return fn ? fn(this, type) : 0;
}

int re::CTFPartyClient::BInvitePlayerToParty(CSteamID)
{
    return 0;
}
int re::CTFPartyClient::BRequestJoinPlayer(CSteamID)
{
    return 0;
}

int re::CTFPartyClient::PromotePlayerToLeader(CSteamID steamid)
{
    typedef int (*PromotePlayerToLeader_t)(re::CTFPartyClient *, CSteamID);
    static auto fn = PromotePlayerToLeader_t(gSignatures.GetClientSignature(sigs::promote_to_leader));
    return fn ? fn(this, steamid) : 0;
}

std::vector<unsigned> re::CTFPartyClient::GetPartySteamIDs()
{
    typedef bool (*SteamIDOfSlot_t)(re::CTFPartyClient *, int, CSteamID *);
    static auto fn = SteamIDOfSlot_t(gSignatures.GetClientSignature(sigs::party_client_get_member_steamid));
    std::vector<unsigned> party_members;
    if (!fn)
        return party_members;
    for (int i = 0; i < GetNumMembers(); i++)
    {
        CSteamID out;
        fn(this, i, &out);
        if (out.GetAccountID())
            party_members.push_back(out.GetAccountID());
    }
    return party_members;
}

int re::CTFPartyClient::KickPlayer(CSteamID steamid)
{
    typedef int (*KickPlayer_t)(re::CTFPartyClient *, CSteamID);
    static auto fn = KickPlayer_t(gSignatures.GetClientSignature(sigs::party_client_kick_player));
    return fn ? fn(this, steamid) : 0;
}

bool re::CTFPartyClient::GetCurrentPartyLeader(CSteamID &id)
{
    uintptr_t party = *reinterpret_cast<uintptr_t *>(reinterpret_cast<uintptr_t>(this) + 0x30);
    if (!party)
        return false;
    id = *reinterpret_cast<CSteamID *>(party + 0x1C);
    return true;
}

re::ITFMatchGroupDescription *re::GetMatchGroupDescription(int &)
{
    return nullptr;
}
