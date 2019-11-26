#include "gtest/gtest.h"
#include "databuffer.h"

TEST(util_databuffer, Construct)
{
    {
        DataBuffer db;

        ASSERT_EQ(db.size, 0);
        ASSERT_EQ(db.used, 0);
        ASSERT_EQ(db.id, 0);
    }

    {
        DataBuffer db(100);
        ASSERT_EQ(db.size, 100);
        ASSERT_EQ(db.used, 0);
        ASSERT_EQ(db.id, 0);
    }
}

TEST(util_databuffer, CopyAssign)
{
    DataBuffer dbSource(100);
    ASSERT_EQ(dbSource.size, 100);
    ASSERT_EQ(dbSource.used, 0);
    ASSERT_EQ(dbSource.id, 0);

    for (size_t i=0; i < dbSource.size; i++)
    {
        dbSource.data[i] = i % std::numeric_limits<u8>::max();
        dbSource.used++;
    }

    ASSERT_EQ(dbSource.used, dbSource.size);

    auto dbCopy = dbSource;

    ASSERT_EQ(dbCopy.size, dbSource.size);
    ASSERT_EQ(dbCopy.used, dbSource.used);

    for (size_t i=0; i < dbSource.size; i++)
    {
        ASSERT_EQ(dbCopy.data[i], dbSource.data[i]);
    }
}

TEST(util_databuffer, CopyConstruct)
{
    DataBuffer dbSource(100);
    ASSERT_EQ(dbSource.size, 100);
    ASSERT_EQ(dbSource.used, 0);
    ASSERT_EQ(dbSource.id, 0);

    for (size_t i=0; i < dbSource.size; i++)
    {
        dbSource.data[i] = i % std::numeric_limits<u8>::max();
        dbSource.used++;
    }

    ASSERT_EQ(dbSource.used, dbSource.size);

    DataBuffer dbCopy(dbSource);

    ASSERT_EQ(dbCopy.size, dbSource.size);
    ASSERT_EQ(dbCopy.used, dbSource.used);

    for (size_t i=0; i < dbSource.size; i++)
    {
        ASSERT_EQ(dbCopy.data[i], dbSource.data[i]);
    }
}
