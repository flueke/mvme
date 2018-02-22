#include "util/tree.h"
#include <benchmark/benchmark.h>
#include <QTextStream>
#include <QDebug>

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

        assert(!root.contains("keyA"));

        root.putBranch("keyA", { "valueA", nodeCount++ });

        assert(root.isRoot());
        assert(!root.isLeaf());
        assert(root.parent() == nullptr);
        assert(root.childCount() == 1);
        assert(root.contains("keyA"));

        auto childNode = root.child("keyA");
        assert(childNode->data().str == "valueA");

        dump_tree(out, root);
    }

    {
        double nodeCount = 0.0;
        Node root;

        assert(!root.contains("keyA"));

        auto nodeA = root.putBranch("keyA", { "valueA", nodeCount++ });
        auto nodeB = root.putBranch("keyB", { "valueB", nodeCount++ });
        auto nodeC = root.putBranch("keyC", { "valueC", nodeCount++ });

        nodeA->putBranch("keyAA", { "valueAA", nodeCount++ });
        nodeA->putBranch("keyAB", { "valueAB", nodeCount++ });
        nodeB->putBranch("keyBA", { "valueBA", nodeCount++ });
        nodeC->putBranch("keyCA", { "valueCA", nodeCount++ });

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

        root.putBranch("a.b.c.d");
        const auto gNode = root.putBranch("e.f.g");
        const auto fNode = root.child("e.f");
        assert(fNode->child("g") == gNode);
        assert(gNode->parent() == fNode);
        root.putBranch("h.i.j.k.l");

        out << endl << ">>>>> Tree >>>>>" << endl;
        dump_tree(out, root);
        out << endl << "<<<<< End Tree <<<<<" << endl;
    }
}
BENCHMARK(TEST_tree_branches);

static void TEST_path_iterator(benchmark::State &state)
{
    struct PathAndResult
    {
        const QString path;
        const QStringList result;
    };

    const QVector<PathAndResult> tests =
    {
        {
            "alpha.beta.gamma.delta", { "alpha", "beta", "gamma", "delta" }
        },
        {
            "1.2.3", { "1", "2", "3" }
        },
        {
            "1", { "1" }
        },
        {
            "1.2.", { "1", "2" }
        },
        {
            ".", {}
        },
        {
            ".1", {}
        },
        {
            ".1.", {}
        },
    };

    for (const auto &test: tests)
    {
        //qDebug() << "=============================================";
        //qDebug() << "<<<<< path =" << test.path;

        util::tree::PathIterator iter(test.path);

        for (const auto &expected: test.result)
        {
            auto part = iter.next();
            //qDebug() << part;
            assert(part == expected);
        }

        assert(iter.next().isEmpty());

        //qDebug() << ">>>>>";
    }
}
BENCHMARK(TEST_path_iterator);

static void TEST_copy_parent_relationship(benchmark::State &state)
{
    // copy
    {
        Node branch;
        branch.putBranch("branch1.a.b");
        branch.assertParentChildIntegrity();

        Node branchCopy(branch);
        branchCopy.assertParentChildIntegrity();
        branch.assertParentChildIntegrity();
    }

    // add branch to tree
    {
        Node destRoot;
        destRoot.putBranch("dest.a.b");
        destRoot.putBranch("dest.a.c");
        destRoot.putBranch("dest.zzz.420");
        destRoot.assertParentChildIntegrity();

        Node sourceNode;
        sourceNode.putBranch("source.x.y.z");
        sourceNode.putBranch("source.x.1.2");
        sourceNode.assertParentChildIntegrity();

        out << endl << ">>>>> Source Tree >>>>>" << endl;
        dump_tree(out, sourceNode);
        out << endl << "<<<<< End Tree <<<<<" << endl;

        Node *destNode = destRoot.child("dest.a");
        *destNode = sourceNode; // copy here. everything below a will be replaced with the children of the source
        sourceNode.assertParentChildIntegrity();
        destNode->assertParentChildIntegrity();
        destRoot.assertParentChildIntegrity();

        out << endl << ">>>>> Final Dest Tree >>>>>" << endl;
        dump_tree(out, destRoot);
        out << endl << "<<<<< End Tree <<<<<" << endl;
    }
}
BENCHMARK(TEST_copy_parent_relationship);

BENCHMARK_MAIN();
