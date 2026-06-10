// Copyright (C) 2019-2020 Tom Zander <tomz@freedommail.ch>
// Copyright (C) 2020-2021 Calin Culianu <calin.culianu@gmail.com>
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#pragma once

#include <bloom.h>
#include <dsproof/dsproof.h>
#include <net_nodeid.h>
#include <sync.h>
#include <util/saltedhashers.h>

#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index_container.hpp>

#include <list>
#include <map>
#include <utility>

class COutPoint;

class DoubleSpendProofStorage
{
public:
    DoubleSpendProofStorage();

    // Note: All public methods below are thread-safe

    // --- Basic Properties

    /// Returns the number of proofs currently stored (both orphan and non-orphan)
    size_t size() const;

    /// Returns the numnber of proofs currently stored that are marked as orphans
    /// This value is always <= .size()
    size_t numOrphans() const;

    // - Orphan expiry - used by the periodic cleaner, if active, to determine when we expire orphans
    static constexpr int defaultSecondsToKeepOrphans() { return 90; }
    int secondsToKeepOrphans() const;
    void setSecondsToKeepOrphans(int secs);

    // - Orphan limit
    static constexpr size_t defaultMaxOrphans() { return 65535; }
    size_t maxOrphans() const;
    void setMaxOrphans(size_t max);

    // H8: per-peer orphan cap. Bounds how many orphan proofs a single peer may
    // have resident in storage, so one peer cannot fill the entire orphan pool
    // and drive eviction churn of other peers' (possibly honest) orphans.
    //
    // The default is a generous fraction (25%) of the global orphan limit:
    // large enough never to interfere with honest peers (a single peer would
    // need thousands of simultaneous unconfirmed double spends to hit it), but
    // small enough that no single peer can monopolize the pool — at least 4
    // distinct peers are required to fill it. A value of 0 disables the
    // per-peer cap entirely.
    static constexpr size_t defaultMaxOrphansPerPeer() { return defaultMaxOrphans() / 4; }
    size_t maxOrphansPerPeer() const;
    void setMaxOrphansPerPeer(size_t max);

    // --- Main Methods

    /// Adds a proof, returns true if it did not exist and was added,
    /// false if it was not added because it already existed.
    ///
    /// Note that "adding" an existing orphan is supported. The proof
    /// will get marked as a non-orphan (equivalent to 'claimOrphan()').
    ///
    /// @throws std::invalid_argument if the supplied proof is empty
    ///     or has a null hash.
    bool add(const DoubleSpendProof &proof);
    /// Remove by proof-id, returns true if proof was found and removed.
    bool remove(const DspId &hash);

    /// add()s and additionally registers the proof as an orphan.
    /// Orphans expire after secondsToKeepOrphans() elapses. They may
    /// be claimed using 'claimOrphan()'. Existing proofs will be
    /// recategorized as an orphan.
    /// @param onlyIfNotExists - if true, only add the orphan if no such
    ///     proof exists. Do not recategorize or otherwise modify existing
    ///     proofs.
    /// @returns true, unless onlyIfNotExists == true, in which case this
    ///     may return false if the proof already existed.
    /// @throws std::invalid_argument if the supplied proof is empty
    ///     or has a null hash.
    bool addOrphan(const DoubleSpendProof &proof, NodeId peerId, bool onlyIfNotExists = false);
    /// Returns all (not yet verified) orphans matching prevOut.
    /// Each item is a pair of a uint256 and the nodeId that send the proof to us.
    std::list<std::pair<DspId, NodeId>> findOrphans(const COutPoint &prevOut) const;

    /// Returns all the proofs known to this storage instance.
    /// For each item, pair.second is true if the proof was flagged as an orphan in storage, false otherwise.
    std::vector<std::pair<DoubleSpendProof, bool>> getAll(bool includeOrphans=false) const;

    /// Flags the proof associated with hash as not an orphan, and thus
    /// not subject to automatic expiry.
    void claimOrphan(const DspId &hash);

    /// Make an existing non-orphan proof into an orphan, putting it back
    /// into the orphan pool. It will expire sometime in the future after
    /// secondsToKeepOrphans() elapses.  It may also be claimed again
    /// in the future using `claimOrphan()`.
    ///
    /// If `hash` does not exist or is already an orphan, this is a no-op.
    void orphanExisting(const DspId &hash);

    /// Lookup a double-spend proof by id.
    /// The returned value will be .isEmpty() if the id was not found.
    DoubleSpendProof lookup(const DspId &hash) const;
    bool exists(const DspId &hash) const;

    // To be installed from a periodic scheduler task. (Returns true)
    // (implemented in storage_cleanup.cpp)
    bool periodicCleanup();

    bool isRecentlyRejectedProof(const DspId &hash) const;
    void markProofRejected(const DspId &hash);
    void newBlockFound();

    /// Completely empties this data structure, clearing all orphans and known proofs
    /// @param clearOrphans if false, orphans will be kept, but everything else will be cleared
    void clear(bool clearOrphans = true);

    ///! Takes all extant proofs and marks them as orphans.
    void orphanAll();

    //! H-3: drop the per-peer orphan *counter* entry for a disconnected peer.
    //! This only removes the bookkeeping entry in m_orphansByPeer so that map
    //! cannot grow without bound as peers churn; it does NOT touch the resident
    //! orphan proofs themselves (those are reaped by the normal expiry path),
    //! so it must not adjust m_numOrphans. Safe to call for a peer with no
    //! entry (no-op). Call this from net_processing when a peer disconnects.
    void dropPeer(NodeId nodeId);

private:
    mutable RecursiveMutex m_lock;

    struct Entry {
        bool orphan = false;
        DoubleSpendProof proof;
        NodeId nodeId = -1;     //! If positive, the bannable peer that told use about this proof.
        int64_t timeStamp = -1;

        struct Id_Getter {
            using result_type = DspId;
            const result_type & operator()(const Entry &e) const { return e.proof.GetId(); }
        };
        struct COutPoint_Getter {
            using result_type = COutPoint;
            const result_type & operator()(const Entry &e) const { return e.proof.outPoint(); }
        };
    };
    struct ModFastFail; //! back(e) predicate for use with index modifier of m_proofs (quits on failure)

    struct tag_COutPoint {}; //! index tag used below
    struct tag_TimeStamp {}; //! index tag used below

    using IndexedProofs = boost::multi_index_container<
        Entry, boost::multi_index::indexed_by<
                    // indexed by dsproof hash
                    boost::multi_index::hashed_unique<
                        Entry::Id_Getter, SaltedUint256Hasher>,
                    // also indexd by COutPoint
                    boost::multi_index::hashed_non_unique<
                        boost::multi_index::tag<tag_COutPoint>, Entry::COutPoint_Getter, SaltedOutpointHasher>,
                    // also sorted by timeStamp
                    boost::multi_index::ordered_non_unique<
                        boost::multi_index::tag<tag_TimeStamp>, boost::multi_index::member<Entry, int64_t, &Entry::timeStamp>>
        >
    >;

    IndexedProofs m_proofs GUARDED_BY(m_lock);
    CRollingBloomFilter m_recentRejects GUARDED_BY(m_lock);

    // Orphan counter and limits
    int m_secondsToKeepOrphans GUARDED_BY(m_lock) = defaultSecondsToKeepOrphans();
    size_t m_maxOrphans GUARDED_BY(m_lock) = defaultMaxOrphans();
    size_t m_numOrphans GUARDED_BY(m_lock) = 0;
    //! H8: per-peer orphan cap, and a per-peer orphan count map kept in sync
    //! with the orphan entries in m_proofs.
    size_t m_maxOrphansPerPeer GUARDED_BY(m_lock) = defaultMaxOrphansPerPeer();
    std::map<NodeId, size_t> m_orphansByPeer GUARDED_BY(m_lock);
    //! H8: adjust the per-peer orphan count map (n may be negative to decrement).
    void adjustPeerOrphanCount(NodeId nodeId, long delta) EXCLUSIVE_LOCKS_REQUIRED(m_lock);
    //! may throw std::runtime_error if number would go below 0
    void decrementOrphans(size_t n) EXCLUSIVE_LOCKS_REQUIRED(m_lock);
    //! implicitly calls checkOrphanLimit()
    void incrementOrphans(size_t n, const DspId &dontDeleteHash) EXCLUSIVE_LOCKS_REQUIRED(m_lock);
    //! if number of orphans is above threshold, will delete old orphans
    void checkOrphanLimit(const DspId &dontDeleteHash) EXCLUSIVE_LOCKS_REQUIRED(m_lock);
};
