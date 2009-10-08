// The subgraph data structure

#include "subgraph.h"
#include "shared_map.h"
#include "real_call.h"
#include "env.h"
#include "util.h"
#include "inverse_map.h"

struct subgraph_entry {
    enum action_type type;
    struct hash data; // meaning depends on type
};

// TODO: Rethink default counts and make them resizable
static struct shared_map subgraph = { "subgraph", sizeof(struct subgraph_entry), 1<<10 };

static const char *subgraph_path()
{
    const char *waitless_dir = getenv(WAITLESS_DIR);
    if (!waitless_dir)
        die("WAITLESS_DIR not set");
    return path_join(waitless_dir, subgraph.name);
}

void subgraph_init()
{
    shared_map_init(&subgraph, real_open(subgraph_path(), O_CREAT | O_WRONLY, 0644));
}

// TODO: thread safety
void initialize()
{
    static int initialized = 0;
    if (initialized)
        return;
    initialized = 1;

    shared_map_open(&subgraph, subgraph_path());
}

char *show_subgraph_node(char s[SHOW_NODE_SIZE], enum action_type type, const struct hash *data)
{
    int n;
    char buffer[SHOW_NODE_SIZE];
    switch (type) {
        case SG_STAT:
            inverse_hash_string(data, buffer, sizeof(buffer));
            n = snprintf(s, SHOW_NODE_SIZE, "stat(\"%s\")", buffer);
            break;
        case SG_READ:
            inverse_hash_string(data, buffer, sizeof(buffer));
            n = snprintf(s, SHOW_NODE_SIZE, "read(\"%s\")", buffer);
            break;
        case SG_WRITE: {
            inverse_hash_memory(data, buffer, sizeof(buffer));
            struct hash *hashes = (struct hash*)buffer;
            char *p = s;
            p += strlcpy(p, "write(\"", s+SHOW_NODE_SIZE-p);
            p += inverse_hash_string(hashes, p, s+SHOW_NODE_SIZE-p);
            p += strlcpy(p, "\", ", s+SHOW_NODE_SIZE-p);
            p = show_hash(p, min(8, s+SHOW_NODE_SIZE-p), hashes+1);
            p += strlcpy(p, ")", s+SHOW_NODE_SIZE-p);
            n = p - s;
            break;
        }
        case SG_FORK:
            n = snprintf(s, SHOW_NODE_SIZE, "fork(%d)", data->data[0] ? 1 : 0);
            break;
        case SG_EXEC: {
            // See action_execve in action.c for data format
            inverse_hash_string(data, buffer, sizeof(buffer));
            char *p = s, *q = buffer;
            p += strlcpy(p, "exec(\"", s+SHOW_NODE_SIZE-p);
            // Copy path
            int c = strlcpy(p, q, s+SHOW_NODE_SIZE-p);
            p += c; q += c+1;
            // Copy argv
            uint32_t argc, i;
            memcpy(&argc, q, sizeof(uint32_t));
            p += strlcpy(p, "\", \"", s+SHOW_NODE_SIZE-p);
            q += 4;
            for (i = 0; i < argc; i++) {
                if (i)
                    *p++ = ' ';
                int c = strlcpy(p, q, s+SHOW_NODE_SIZE-p);
                p += c; q += c + 1;
            }
            // Finish up, noting whether we're connected via a pipe
            p += strlcpy(p, *q ? "\")" : "\", <pipe>)", s+SHOW_NODE_SIZE-p);
            n = p - s;
            break;
        }
        case SG_EXIT:
            n = snprintf(s, SHOW_NODE_SIZE, "exit(%d)", data->data[0]);
            break;
        default:
            n = snprintf(s, SHOW_NODE_SIZE, "unknown type %d", type);
    }
    return s + min(n, SHOW_NODE_SIZE - 1);
}

void subgraph_new_node(const struct hash *name, enum action_type type, const struct hash *data)
{
    initialize();

    shared_map_lock(&subgraph);  
    struct subgraph_entry *entry;
    if (!shared_map_lookup(&subgraph, name, (void**)&entry, 1)) {
        // New entry
        entry->type = type;
        entry->data = *data;
    }
    else {
        if (entry->type != type || !hash_equal(&entry->data, data)) {
            char hash[SHOW_HASH_SIZE], old[SHOW_NODE_SIZE], new[SHOW_NODE_SIZE];
            show_hash(hash, 8, name);
            show_subgraph_node(old, entry->type, &entry->data);
            show_subgraph_node(new, type, data);
            die("nondeterminism detected at node %s:\n  old: %s\n  new: %s", hash, old, new);
        }
    }
    shared_map_unlock(&subgraph);
}

static int dump_helper(const struct hash *name, void *value)
{
    struct subgraph_entry *entry = value;
    char hash[SHOW_HASH_SIZE], node[SHOW_NODE_SIZE];
    show_hash(hash, 8, name);
    show_subgraph_node(node, entry->type, &entry->data);
    fdprintf(STDOUT_FILENO, "  %s: %s\n", hash, node);
    return 0;
}

void subgraph_dump()
{
    initialize();

    shared_map_lock(&subgraph);
    write_str(STDOUT_FILENO, "subgraph dump:\n");
    shared_map_iter(&subgraph, dump_helper);
    shared_map_unlock(&subgraph);
}
