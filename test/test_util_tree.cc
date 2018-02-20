#include "util/tree.h"
#include <benchmark/benchmark.h>
#include <QTextStream>

struct TreeData
{
    QString str;
    double d = 0.0;
};

using Node = util::tree::Node<TreeData>;
QTextStream out(stdout);

static void TEST_create_node(benchmark::State &state)
{
    {
        Node root;

        assert(root.isRoot());
        assert(root.isLeaf());
        assert(root.parent() == nullptr);
        assert(root.childCount() == 0);

        assert(root.data().str.isEmpty());
        assert(root.data().d == 0.0);
    }

    {
        Node root({"Hello, world!", 42.0});

        assert(root.isRoot());
        assert(root.isLeaf());
        assert(root.parent() == nullptr);
        assert(root.childCount() == 0);

        assert(root.data().str == "Hello, world!");
        assert(root.data().d == 42.0);
    }
}
BENCHMARK(TEST_create_node);

static void TEST_tree_basic(benchmark::State &state)
{
    {
        double nodeCount = 0.0;
        Node root;

        assert(!root.hasChild("keyA"));

        root.addDirectChild("keyA", { "valueA", nodeCount++ });

        assert(root.isRoot());
        assert(!root.isLeaf());
        assert(root.parent() == nullptr);
        assert(root.childCount() == 1);
        assert(root.hasChild("keyA"));

        auto &childNode = root.getChild("keyA");
        assert(childNode.data().str == "valueA");

        dump_tree(out, root);
    }

    {
        double nodeCount = 0.0;
        Node root;

        assert(!root.hasChild("keyA"));

        auto &nodeA = root.addDirectChild("keyA", { "valueA", nodeCount++ });
        auto &nodeB = root.addDirectChild("keyB", { "valueB", nodeCount++ });
        auto &nodeC = root.addDirectChild("keyC", { "valueC", nodeCount++ });

        nodeA.addDirectChild("keyAA", { "valueAA", nodeCount++ });
        nodeA.addDirectChild("keyAB", { "valueAB", nodeCount++ });
        nodeB.addDirectChild("keyBA", { "valueBA", nodeCount++ });
        nodeC.addDirectChild("keyCA", { "valueCA", nodeCount++ });

        out << endl << ">>>>> Tree:" << endl;
        dump_tree(out, root);
        out << endl << "<<<<< End Tree" << endl;
    }
}
BENCHMARK(TEST_tree_basic);

static void TEST_tree_branches(benchmark::State &state)
{
    {
        double nodeCount = 0.0;
        Node root;

        root.createBranch("a.b.c.d");
        root.createBranch("e.f.g");
        root.createBranch("h.i.j.k.l");

        out << endl << ">>>>> Tree:" << endl;
        dump_tree(out, root);
        out << endl << "<<<<< End Tree" << endl;
    }
}
BENCHMARK(TEST_tree_branches);

BENCHMARK_MAIN();
