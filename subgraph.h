// The subgraph data structure

#ifndef __subgraph_h__
#define __subgraph_h__

/*
 * The structure of the subgraph:
 *
 * In order to simplify reasoning about dependencies across multiple runs, we
 * view our current dependency knowledge as a subgraph of a conceptually
 * infinite, immutable graph describing the dependencies of all possible
 * process runs.  This graph has the following types of nodes and one directed
 * edge for each direct dependency between nodes:
 *
 * 1. File nodes: These represent the contents of one particular file, which
 *    possibly doesn't exist.  File nodes are named by the hash of their
 *    contents, and the nonexistent file node has name zero.
 *
 *    File nodes don't need to contain the path to the file because we always
 *    know the path from the surrounding context.  For example, if a file node
 *    is the parent of a node which is the child of a read node, the file path
 *    will be stored in the read node.
 *
 * 2. Process nodes: These represent the state of a process as it starts to
 *    perform an action.  The name of a process node C is
 *
 *        name(C) = hash(name(P1), ..., name(Pn))
 *
 *    where P1, ..., Pn are the parents of C.  We store the action performed
 *    along with each node for replay purposes.  Note that the action is
 *    *not* part of the name of the node because the action is a consequence
 *    of the parents.  Nonconstructively, if the action was part of the name
 *    of the node, the name of the node would be impossible to compute if you
 *    only knew the parents, so it can't be part of the name.  The types of
 *    actions are
 *
 *        stat(path) - check if a file exists
 *        read(path) - open and read path if the file exists
 *        write(path, hash(contents)) - write contents to path
 *        fork(1) - the two children are named hash(name(P), 0) and
 *            hash(name(P), 1) for the child and parent processes, respectively.
 *        fork(0) - parent and child are joined by a pipe, so their subgraph
 *            nodes are interleaved.
 *        exec(path, argv, envp) - become a new process
 *        exec(path) - become a new process with a link to the parent, so that
              subgraph nodes are interleaved as with fork(0)
 *        wait(...) - TODO: figure out waits (yes, sometimes we want these)
 *            These are complicated because of the slew of different wait calls.
 *        exit(status) - exit with the given status
 *
 * TODO: The above naming scheme allows file nodes and process nodes to
 * tractably have the same name (just make a file containing a few hash values).
 * This overlap is okay iff we always know which kind of node we're dealing with
 * from context.  I believe that can be made true with care; it seems like the
 * concrete subgraph data structure will only have concrete entries for process
 * nodes, and any information attached to file nodes goes in the snapshot.  If
 * true, the safety of the overlap should follow from structural induction.
 *
 * The subgraph is a subset of nodes of the original graph with the invariant
 * that if a process node C exists in the subgraph all its parents do as well.
 */

/*
 * Background:
 *
 * Consider a single process moving from time t to time t+1.  We define the
 * state of the process at each of these instants in time to be the memory
 * contents of the process plus any per-process kernel information (file
 * descriptors, etc.).  The state at time t+1 depends on three things:
 *
 * 1. The state of the process at time t.
 * 2. The behavior of the host hardware between time t and t+1.
 * 3. Any inputs between time t and t+1.
 *
 * We further divide the information content of each of these inputs into two
 * parts, which we call "deterministic" and "nondeterministic".  These are
 * defined nonconstructively as follows:
 *
 * 1. The "deterministic" part is any information which want to include as part
 *    of the dependencies of the process. 
 *
 * 2. The "nondeterministic" part is any information which want to *ignore* as
 *    part of the dependencies of the process.
 *
 * We give two specific examples of processes to illustrate these concepts:
 *
 * 1. sed: The deterministic inputs to sed are:
 *
 *    a. The command line
 *    b. The sed elf file itself and associated libraries.
 *    c. The contents of the input files, which may be stdin.
 *
 *    The nondeterministic inputs include
 *
 *    a. Asynchronous delays in the input pipe, manifesting as the number of
 *    bytes received by each call to read.
 *    b. Whether the user hit ^C while sed was running.
 *    c. NFS timeouts.
 *
 * 2. pdflatex: The deterministic inputs to pdflatex include:
 *
 *    a. The various input files, including auxiliary files spit out by a
 *       previous invocation of pdflatex.
 *    b. Command line, elf, etc.
 *
 *    The most interesting nondeterminstic input is
 *
 *    a. The time of day, which pdflatex merrily inserts into the pdf file
 *       (thanks to Dylan for this observation).
 *
 * We expect waitless to detect all the deterministic inputs and carefully
 * account for them, but we also need to do something reasonable for
 * nondeterministic inputs.  We can further subdivide nondeterministic inputs
 * into two classes:
 *
 * 1. Harmless nondeterministic inputs are those where it is okay to produce
 *    a result deriving from a _different_ value of the input.  The best
 *    example of this is the time of day: we usually wouldn't mind if our build
 *    process detected an old version of a .tex document and resurrected the
 *    corresponding old version of the .pdf result.
 *
 * 2. Dangerous nondeterministic inputs are those which cause the output to be
 *    useles.  The best example of this is ^C; we do not want to cache the fact
 *    that the process died and replay this behavior the next time the user
 *    invokes waitless.
 *
 * We heuristically distinguish between harmless and dangerous inputs based on
 * the exit status of the process:
 *
 * 1. If a process exits with zero status, all its nondeterministic inputs were
 *    harmless.
 *
 * 2. If a process exits with nonzero status, sys/wait.h defines various useful
 *    macros:
 *
 *    a. WIFEXITED - the process exited normally via _exit(2) or exit(3).
 *       It probably received only harmless nondeterministic inputs, but we
 *       can't be certain.  We default to treating the inputs as harmless,
 *       but provide a command line option to waitless to treat this case as
 *       dangerous (TODO: add the --treat-all-nonzero-exits-as-dangerous flag)

 *    b. WIFSIGNALED - the process terminated due to receipt of a signal.
 *       It must have received a dangerous nondeterministic input.
 *
 *    c. WIFSTOPPED - the process has not terminated, but has stopped and
 *       can be restarted (only happens if WUNTRACED was specified, which
 *       we disallow (TODO: make sure we disallow WUNTRACED on wait calls)
 *
 * Note that bugs in the process are captured as deterministic inputs, since
 * they are contained in the machine code of the executable and its associated
 * libraries.  This is true even if the bugs manifest in nondeterministic ways;
 * if you fix the bug, all of the potential cached nondeterministic buggy
 * results will no longer apply.
 *
 * Statically linked process are one example: LD_PRELOAD doesn't work on
 * processes statically linked against glibc, so waitless will fail to detect
 * their inputs.  Therefore, we consider the inputs of statically linked
 * processes to be entirely nondeterministic, detect the fact that an executable
 * is statically linked as part of normally determinstic input detection and
 * provide a warning to the user.  If the user relinks the process, the change
 * will be detected as a modified deterministic input and rebuild will happen
 * automatically.
 * TODO: make sure we warn about statically linked libraries
 */

#include "hash.h"

// Different types of actions / subgraph process nodes
enum action_type {
    SG_STAT  = 1,
    SG_READ  = 2,
    SG_WRITE = 3,
    SG_FORK  = 4,
    SG_EXEC  = 5,
    SG_WAIT  = 6,
    SG_EXIT  = 7,
};

// Create the subgraph on disk necessary
extern void subgraph_init();

// Compute the name of a process node given its parents
static inline void subgraph_node_name(struct hash *name, const struct hash *parents, int n)
{
    hash_memory(name, parents, n * sizeof(struct hash));
}

extern void subgraph_new_node(const struct hash *name, enum action_type type, const struct hash *data);

extern void subgraph_dump();

#define SHOW_NODE_SIZE 1024
// Returns a pointer to the trailing null (like stpcpy)
extern char *show_subgraph_node(char s[SHOW_NODE_SIZE], enum action_type type, const struct hash *data);

#endif
