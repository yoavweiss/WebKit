/*
 * Copyright (C) 2015-2021 Apple Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL APPLE INC. OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#pragma once

#include "IDBIndexIdentifier.h"
#include "IDBKeyData.h"
#include "IDBObjectStoreInfo.h"
#include "IndexKey.h"
#include "MemoryIndex.h"
#include "MemoryObjectStoreCursor.h"
#include "ThreadSafeDataBuffer.h"
#include <wtf/HashMap.h>
#include <wtf/RefCountedAndCanMakeWeakPtr.h>

namespace WebCore {

class IDBCursorInfo;
class IDBError;
class IDBGetAllResult;
class IDBKeyData;
class IDBValue;

struct IDBKeyRangeData;

namespace IndexedDB {
enum class GetAllType : bool;
enum class IndexRecordType : bool;
}

namespace IDBServer {

class MemoryBackingStoreTransaction;

typedef HashMap<IDBKeyData, ThreadSafeDataBuffer, IDBKeyDataHash, IDBKeyDataHashTraits> KeyValueMap;

class MemoryObjectStore : public RefCountedAndCanMakeWeakPtr<MemoryObjectStore> {
public:
    static Ref<MemoryObjectStore> create(const IDBObjectStoreInfo&);

    ~MemoryObjectStore();

    void transactionFinished(MemoryBackingStoreTransaction&);
    void writeTransactionStarted(MemoryBackingStoreTransaction&);
    void writeTransactionFinished(MemoryBackingStoreTransaction&);
    void transactionAborted(MemoryBackingStoreTransaction&);
    MemoryBackingStoreTransaction* writeTransaction();

    IDBError addIndex(MemoryBackingStoreTransaction&, const IDBIndexInfo&);
    void revertAddIndex(MemoryBackingStoreTransaction&, IDBIndexIdentifier);
    IDBError updateIndexRecordsWithIndexKey(MemoryBackingStoreTransaction&, const IDBIndexInfo&, const IDBKeyData&, const IndexKey&);
    IDBError deleteIndex(MemoryBackingStoreTransaction&, IDBIndexIdentifier);
    void forEachRecord(Function<void(const IDBKeyData&, const IDBValue&)>&&);
    void deleteAllIndexes(MemoryBackingStoreTransaction&);
    void registerIndex(Ref<MemoryIndex>&&);

    bool containsRecord(const IDBKeyData&);
    void deleteRecord(const IDBKeyData&);
    void deleteRange(const IDBKeyRangeData&);
    IDBError addRecord(MemoryBackingStoreTransaction&, const IDBKeyData&, const IndexIDToIndexKeyMap&, const IDBValue&);

    uint64_t currentKeyGeneratorValue() const { return m_keyGeneratorValue; }
    void setKeyGeneratorValue(uint64_t value) { m_keyGeneratorValue = value; }

    void clear();

    ThreadSafeDataBuffer valueForKey(const IDBKeyData&) const;
    ThreadSafeDataBuffer valueForKeyRange(const IDBKeyRangeData&) const;
    IDBKeyData lowestKeyWithRecordInRange(const IDBKeyRangeData&) const;
    IDBGetResult indexValueForKeyRange(IDBIndexIdentifier, IndexedDB::IndexRecordType, const IDBKeyRangeData&) const;
    uint64_t countForKeyRange(std::optional<IDBIndexIdentifier>, const IDBKeyRangeData&) const;

    void getAllRecords(const IDBKeyRangeData&, std::optional<uint32_t> count, IndexedDB::GetAllType, IDBGetAllResult&) const;

    const IDBObjectStoreInfo& info() const { return m_info; }
    IDBObjectStoreInfo& info() { return m_info; }

    MemoryObjectStoreCursor* maybeOpenCursor(const IDBCursorInfo&, MemoryBackingStoreTransaction&);

    IDBKeyDataSet* orderedKeys() { return m_orderedKeys.get(); }

    MemoryIndex* indexForIdentifier(IDBIndexIdentifier);

    void maybeRestoreDeletedIndex(Ref<MemoryIndex>&&);

    void rename(const String& newName) { m_info.rename(newName); }
    void renameIndex(MemoryIndex&, const String& newName);

    RefPtr<MemoryIndex> takeIndexByIdentifier(IDBIndexIdentifier);

private:
    MemoryObjectStore(const IDBObjectStoreInfo&);

    IDBKeyDataSet::iterator lowestIteratorInRange(const IDBKeyRangeData&, bool reverse) const;

    IDBError updateIndexesForPutRecord(const IDBKeyData&, const IndexIDToIndexKeyMap&);
    void updateIndexesForDeleteRecord(const IDBKeyData& value);
    void updateCursorsForPutRecord(IDBKeyDataSet::iterator);
    void updateCursorsForDeleteRecord(const IDBKeyData&);
    void addRecordWithoutUpdatingIndexes(MemoryBackingStoreTransaction&, const IDBKeyData&, const IDBValue&);

    IDBObjectStoreInfo m_info;

    WeakPtr<MemoryBackingStoreTransaction> m_writeTransaction;
    uint64_t m_keyGeneratorValueBeforeTransaction { 1 };
    uint64_t m_keyGeneratorValue { 1 };

    KeyValueMap m_transactionModifiedRecords;
    KeyValueMap m_keyValueStore;
    std::unique_ptr<IDBKeyDataSet> m_orderedKeys;

    HashMap<IDBIndexIdentifier, RefPtr<MemoryIndex>> m_indexesByIdentifier;
    HashMap<String, RefPtr<MemoryIndex>> m_indexesByName;
    HashMap<IDBResourceIdentifier, RefPtr<MemoryObjectStoreCursor>> m_cursors;
};

} // namespace IDBServer
} // namespace WebCore
