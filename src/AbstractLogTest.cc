/* Copyright (c) 2010-2012 Stanford University
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR(S) DISCLAIM ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL AUTHORS BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include "TestUtil.h"

#include "Segment.h"
#include "ServerRpcPool.h"
#include "Log.h"
#include "LogEntryTypes.h"
#include "Memory.h"
#include "ServerConfig.h"
#include "StringUtil.h"
#include "TestLog.h"
#include "Transport.h"

namespace RAMCloud {

class DoNothingHandlers : public LogEntryHandlers {
  public:
    uint32_t getTimestamp(LogEntryType type, Buffer& buffer) { return 0; }
    void relocate(LogEntryType type, Buffer& oldBuffer,
                  Log::Reference oldReference, LogEntryRelocator& relocator) { }
};

/**
 * Unit tests for Log.
 */
class AbstractLogTest : public ::testing::Test {
  public:
    Context context;
    ServerId serverId;
    ServerList serverList;
    ServerConfig serverConfig;
    ReplicaManager replicaManager;
    MasterTableMetadata masterTableMetadata;
    SegletAllocator allocator;
    SegmentManager segmentManager;
    DoNothingHandlers entryHandlers;
    Log l;

    AbstractLogTest()
        : context(),
          serverId(ServerId(57, 0)),
          serverList(&context),
          serverConfig(ServerConfig::forTesting()),
          replicaManager(&context, &serverId, 0, false),
          masterTableMetadata(),
          allocator(&serverConfig),
          segmentManager(&context, &serverConfig, &serverId,
                         allocator, replicaManager, &masterTableMetadata),
          entryHandlers(),
          l(&context, &serverConfig, &entryHandlers,
            &segmentManager, &replicaManager)
    {
        l.sync();
    }

  private:
    DISALLOW_COPY_AND_ASSIGN(AbstractLogTest);
};

TEST_F(AbstractLogTest, constructor) {
    SegletAllocator allocator2(&serverConfig);
    SegmentManager segmentManager2(&context, &serverConfig, &serverId,
                                   allocator2, replicaManager,
                                   &masterTableMetadata);
    Log l2(&context, &serverConfig, &entryHandlers,
           &segmentManager2, &replicaManager);
    EXPECT_EQ(static_cast<LogSegment*>(NULL), l2.head);
}

TEST_F(AbstractLogTest, append_basic) {
    uint32_t dataLen = serverConfig.segmentSize / 2 + 1;
    char* data = new char[dataLen];
    LogSegment* oldHead = l.head;

    int appends = 0;
    while (l.append(LOG_ENTRY_TYPE_OBJ, data, dataLen)) {
        if (appends++ == 0)
            EXPECT_EQ(oldHead, l.head);
        else
            EXPECT_NE(oldHead, l.head);
        oldHead = l.head;
    }
    // This depends on ServerConfig's number of bytes allocated to the log.
    EXPECT_EQ(239, appends);

    // getEntry()'s test ensures actual data gets there.

    delete[] data;
}

static bool
appendFilter(string s)
{
    return s == "append";
}

TEST_F(AbstractLogTest, append_tooBigToEverFit) {
    TestLog::Enable _(appendFilter);

    char* data = new char[serverConfig.segmentSize + 1];
    LogSegment* oldHead = l.head;

    EXPECT_THROW(l.append(LOG_ENTRY_TYPE_OBJ,
                          data,
                          serverConfig.segmentSize + 1),
        FatalError);
    EXPECT_NE(oldHead, l.head);
    EXPECT_EQ("append: Entry too big to append to log: 131073 bytes of type 2",
        TestLog::get());
    delete[] data;
}

TEST_F(AbstractLogTest, append_multiple_basics) {
    Log::AppendVector v[2];

    uint32_t dataLen = serverConfig.segmentSize / 3;
    char* data = new char[dataLen];

    v[0].type = LOG_ENTRY_TYPE_OBJ;
    v[0].buffer.append(data, dataLen);
    v[1].type = LOG_ENTRY_TYPE_OBJTOMB;
    v[1].buffer.append(data, dataLen - 1);

    int appends = 0;
    while (l.append(v, 2)) {
        Buffer buffer;
        EXPECT_EQ(LOG_ENTRY_TYPE_OBJ, l.getEntry(v[0].reference, buffer));
        EXPECT_EQ(dataLen, buffer.getTotalLength());
        buffer.reset();
        EXPECT_EQ(LOG_ENTRY_TYPE_OBJTOMB, l.getEntry(v[1].reference, buffer));
        EXPECT_EQ(dataLen - 1, buffer.getTotalLength());
        appends++;
    }
    // This depends on ServerConfig's number of bytes allocated to the log.
    EXPECT_EQ(239, appends);

    delete[] data;
}

TEST_F(AbstractLogTest, free) {
    // Currently nothing to do - it just passes through to SegmentManager
}

TEST_F(AbstractLogTest, getEntry) {
    uint64_t data = 0x123456789ABCDEF0UL;
    Buffer sourceBuffer;
    sourceBuffer.append(&data, sizeof(data));
    Log::Reference ref;
    EXPECT_TRUE(l.append(LOG_ENTRY_TYPE_OBJ, sourceBuffer, &ref));

    LogEntryType type;
    Buffer buffer;
    type = l.getEntry(ref, buffer);
    EXPECT_EQ(LOG_ENTRY_TYPE_OBJ, type);
    EXPECT_EQ(sizeof(data), buffer.getTotalLength());
    EXPECT_EQ(data, *buffer.getStart<uint64_t>());
}

TEST_F(AbstractLogTest, getSegmentId) {
    Buffer buffer;
    char data[1000];
    buffer.append(data, sizeof(data));
    Log::Reference reference;

    int zero = 0, one = 0, two = 0, other = 0;
    while (l.head == NULL || l.head->id == 1) {
        EXPECT_TRUE(l.append(LOG_ENTRY_TYPE_OBJ, buffer, &reference));
        switch (l.getSegmentId(reference)) {
            case 0: zero++; break;
            case 1: one++; break;
            case 2: two++; break;
            default: other++; break;
        }
    }

    EXPECT_EQ(0, zero);
    EXPECT_EQ(130, one);
    EXPECT_EQ(1, two);
    EXPECT_EQ(0, other);
}

TEST_F(AbstractLogTest, segmentExists) {
    EXPECT_FALSE(l.segmentExists(0));
    EXPECT_TRUE(l.segmentExists(1));
    EXPECT_FALSE(l.segmentExists(2));
    EXPECT_FALSE(l.segmentExists(3));

    char data[1000];
    while (l.head == NULL || l.head->id == 1)
        l.append(LOG_ENTRY_TYPE_OBJ, data, sizeof(data));
    l.sync();


    EXPECT_FALSE(l.segmentExists(0));
    EXPECT_TRUE(l.segmentExists(1));
    EXPECT_TRUE(l.segmentExists(2));
    EXPECT_FALSE(l.segmentExists(3));
}

/// A mostly unusable implementation of AbstractLog that's just good enough
/// to text allocNewWritableHead().
class MockLog : public AbstractLog {
  public:
    explicit MockLog(Log* l)
        : AbstractLog(l->entryHandlers,
                            l->segmentManager,
                            l->replicaManager,
                            0)
        , emptySegletVector()
        , returnSegment(false)
        , segment(emptySegletVector, 2, 2, 1983, 12, 0, false)
    {
    }

    LogSegment*
    allocNextSegment(bool mustNotFail)
    {
        if (mustNotFail)
            RAMCLOUD_TEST_LOG("mustNotFail");
        if (returnSegment)
            return &segment;
        return NULL;
    }

  private:
    vector<Seglet*> emptySegletVector;

  public:
    bool returnSegment;
    LogSegment segment;
};

TEST_F(AbstractLogTest, allocNewWritableHead) {
    MockLog ml(&l);

    ml.head = reinterpret_cast<LogSegment*>(0xdeadbeef);
    EXPECT_FALSE(ml.metrics.noSpaceTimer);
    EXPECT_FALSE(ml.allocNewWritableHead());
    EXPECT_EQ(reinterpret_cast<LogSegment*>(0xdeadbeef), ml.head);
    EXPECT_TRUE(ml.metrics.noSpaceTimer);

    ml.returnSegment = true;
    EXPECT_TRUE(ml.allocNewWritableHead());
    EXPECT_EQ(&ml.segment, ml.head);
    EXPECT_FALSE(ml.metrics.noSpaceTimer);
    EXPECT_NE(0U, ml.metrics.totalNoSpaceTicks);

    *const_cast<bool*>(&ml.head->isEmergencyHead) = true;
    EXPECT_FALSE(ml.allocNewWritableHead());
    EXPECT_EQ(&ml.segment, ml.head);
    EXPECT_TRUE(ml.metrics.noSpaceTimer);

    // Ensure mustNotFail was false in allocNextSegment()
    EXPECT_EQ("", TestLog::get());
}

TEST_F(AbstractLogTest, reference_constructors) {
    Log::Reference empty;
    EXPECT_EQ(0U, empty.value);

    Log::Reference fromInt(2834238428234UL);
    EXPECT_EQ(2834238428234UL, fromInt.value);

    Log::Reference slotOffset(0, 0, 8*1024*1024);
    EXPECT_EQ(0U, slotOffset.value);

    Log::Reference slotOffset2(1, 0, 8*1024*1024);
    EXPECT_EQ(1U << 23, slotOffset2.value);

    Log::Reference slotOffset3(1, 1, 8*1024*1024);
    EXPECT_EQ((1U << 23) | 1, slotOffset3.value);

    Log::Reference slotOffset4(1, 1, 8);
    EXPECT_EQ((1U << 3) | 1, slotOffset4.value);
}

TEST_F(AbstractLogTest, reference_getSlot) {
    for (int i = 1; i < 32; i++) {
        uint32_t segSize = 1U << i;
        Log::Reference ref(15, segSize - 1, segSize);
        EXPECT_EQ(15U, ref.getSlot(segSize));
    }

    Log::Reference ref(0xaaaaaaaa, 0x55555555, 1UL << 31);
    EXPECT_EQ(0xaaaaaaaa, ref.getSlot(1UL << 31));
}

TEST_F(AbstractLogTest, reference_getOffset) {
    for (int i = 1; i < 32; i++) {
        uint32_t segSize = 1U << i;
        Log::Reference ref(15, segSize - 1, segSize);
        EXPECT_EQ(segSize - 1, ref.getOffset(segSize));
    }

    Log::Reference ref(0xaaaaaaaa, 0x55555555, 1UL << 31);
    EXPECT_EQ(0x55555555U, ref.getOffset(1UL << 31));

}

} // namespace RAMCloud
