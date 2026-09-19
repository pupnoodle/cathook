#pragma once

#include <unordered_map>
//#include <cstring>
#include <string.h>
#include <memory>
#include "core/logging.hpp"

// this and the cpp are creds to "Altimor"

class RecvTable;
class RecvProp;

class equal_char
{
public:
    bool operator()(const char *const &v1, const char *const &v2) const
    {
        return !strcmp(v1, v2);
    }
};

struct hash_char
{
public:
    size_t operator()(const char *obj) const
    {
        size_t res         = 0;
        const size_t prime = 31;
        for (size_t i = 0; i < strlen(obj); ++i)
        {
            res = obj[i] + (res * prime);
        }
        return res;
    }
};

class netvar_tree
{
    struct node;
    using map_type = boost::unordered_flat_map<const char *, std::shared_ptr<node>, hash_char, equal_char>;

    struct node
    {
        node(int offset, RecvProp *p) : offset(offset), prop(p)
        {
        }

        map_type nodes;
        int offset;
        RecvProp *prop;
    };

    map_type nodes;

public:
    // netvar_tree ( );

    void init();

private:
    void populate_nodes(class RecvTable *recv_table, map_type *map);

    /**
     * get_offset_recursive - Return the offset of the final node
     * @map:	Node map to scan
     * @acc:	Offset accumulator
     * @name:	Netvar name to search for
     *
     * Get the offset of the last netvar from map and return the sum of it and
     * accum
     */
    int get_offset_recursive(map_type &map, int acc, const char *name, int depth = 0)
    {
        if (depth > 32)
            return 0;
        if (auto it = map.find(name); it != map.end())
            return acc + it->second->offset;
        for (auto &kv : map)
        {
            if (kv.second->nodes.empty())
                continue;
            int nested = get_offset_recursive(kv.second->nodes, acc + kv.second->offset, name, depth + 1);
            if (nested)
                return nested;
        }
        return 0;
    }

    /**
     * get_offset_recursive - Recursively grab an offset from the tree
     * @map:	Node map to scan
     * @acc:	Offset accumulator
     * @name:	Netvar name to search for
     * @args:	Remaining netvar names
     *
     * Descend into nested datatables when an intermediate name is omitted so
     * lookups stay valid across recvtable reshuffles.
     */
    template <typename... args_t> int get_offset_recursive(map_type &map, int acc, const char *name, args_t... args)
    {
        return get_offset_recursive_path(map, acc, 0, name, args...);
    }

    int get_offset_recursive_path(map_type &map, int acc, int depth, const char *name)
    {
        return get_offset_recursive(map, acc, name, depth);
    }

    template <typename... args_t> int get_offset_recursive_path(map_type &map, int acc, int depth, const char *name, args_t... args)
    {
        if (depth > 32)
            return 0;
        if (auto it = map.find(name); it != map.end())
            return get_offset_recursive_path(it->second->nodes, acc + it->second->offset, depth + 1, args...);
        for (auto &kv : map)
        {
            if (kv.second->nodes.empty())
                continue;
            int nested = get_offset_recursive_path(kv.second->nodes, acc + kv.second->offset, depth + 1, name, args...);
            if (nested)
                return nested;
        }
        return 0;
    }

    RecvProp *get_prop_recursive(map_type &map, const char *name)
    {
        if (auto it = map.find(name); it != map.end())
            return it->second->prop;
        for (auto &kv : map)
        {
            if (kv.second->nodes.empty())
                continue;
            if (RecvProp *nested = get_prop_recursive(kv.second->nodes, name))
                return nested;
        }
        return nullptr;
    }

    template <typename... args_t> RecvProp *get_prop_recursive(map_type &map, const char *name, args_t... args)
    {
        if (auto it = map.find(name); it != map.end())
            return get_prop_recursive(it->second->nodes, args...);
        for (auto &kv : map)
        {
            if (kv.second->nodes.empty())
                continue;
            if (RecvProp *nested = get_prop_recursive(kv.second->nodes, name, args...))
                return nested;
        }
        return nullptr;
    }

public:
    /**
     * get_offset - Get the offset of a netvar given a list of branch names
     * @name:	Top level datatable name
     * @args:	Remaining netvar names
     *
     * Initiate a recursive search down the branch corresponding to the
     * specified datable name
     */
    template <typename... args_t> int get_offset(const char *name, args_t... args)
    {
        const auto &node = nodes[name];
        if (node == 0)
        {
            logging::Info("Invalid NetVar node: %s", name);
            return 0;
        }
        int offset = get_offset_recursive(node->nodes, node->offset, args...);
        if (!offset)
            logging::Info("can't find netvar %s", name);
        return offset;
    }

    template <typename... args_t> RecvProp *get_prop(const char *name, args_t... args)
    {
        const auto &node = nodes[name];
        if (node == 0)
            return nullptr;
        return get_prop_recursive(node->nodes, args...);
    }

    void dump();
};

extern netvar_tree gNetvars;
