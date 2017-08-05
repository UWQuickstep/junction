/*------------------------------------------------------------------------
  Junction: Concurrent data structures in C++
  Copyright (c) 2016 Jeff Preshing

  Distributed under the Simplified BSD License.
  Original location: https://github.com/preshing/junction

  This software is distributed WITHOUT ANY WARRANTY; without even the
  implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
  See the LICENSE file for more information.
------------------------------------------------------------------------*/

#include <junction/Core.h>
#include <turf/CPUTimer.h>
#include <turf/Util.h>
#include <turf/extra/UniqueSequence.h>
#include <turf/extra/JobDispatcher.h>
#include <turf/extra/Options.h>
#include <junction/extra/MapAdapter.h>
#include <algorithm>
#include <vector>
#include <stdio.h>

using namespace turf::intTypes;
typedef junction::extra::MapAdapter MapAdapter;

static const ureg NumKeysPerThread = 2000;
static const ureg DefaultReadsPerWrite = 4;
//static const ureg DefaultReadsPerWrite = 1;
static const ureg DefaultItersPerChunk = 10000;
//static const ureg DefaultItersPerChunk = 5000;
static const ureg DefaultChunks = 200;
//static const ureg DefaultChunks = 20;
static const u32 Prime = 0x4190ab09;
static const ureg DefaultWritesPerRead = 1;
static const ureg DefaultInsertsPerRemove = 1;
static const ureg DefaultRemovesPerInsert = 1;


struct SharedState {
    MapAdapter& adapter;
    MapAdapter::Map* map;
    ureg numKeysPerThread;
    ureg numThreads;
    ureg readsPerWrite;
    ureg writesPerRead;
    ureg itersPerChunk;
    ureg insertsPerRemove;
    ureg removesPerInsert;
    turf::extra::SpinKicker spinKicker;
    turf::Atomic<u32> doneFlag;

    SharedState(MapAdapter& adapter, ureg numKeysPerThread, ureg readsPerWrite, ureg writesPerRead, ureg itersPerChunk, ureg insertsPerRemove, ureg removesPerInsert)
        : adapter(adapter), map(NULL), numKeysPerThread(numKeysPerThread), readsPerWrite(readsPerWrite), writesPerRead(writesPerRead),
          itersPerChunk(itersPerChunk), insertsPerRemove(insertsPerRemove), 
          removesPerInsert(removesPerInsert) {
        doneFlag.storeNonatomic(0);
        numThreads = 0;
    }
};

class ThreadState {
public:
    SharedState* m_shared;
    MapAdapter::ThreadContext m_threadCtx;
    ureg m_threadIndex;
    u32 m_rangeLo;
    u32 m_rangeHi;

    u32 m_addIndex;
    u32 m_removeIndex;

    struct Stats {
        ureg mapOpsDone;
        ureg mapWritesDone;
        ureg mapReadsDone;
        double duration;
        ureg mapInsertsDone;
        double totInsertTime;
        ureg mapRemovesDone;
        double totRemoveTime;

        Stats() {
            mapOpsDone = 0;
            mapReadsDone = 0;
            mapWritesDone = 0;
            duration = 0;
            mapInsertsDone = 0;
            totInsertTime = 0;
            mapRemovesDone = 0;
            totRemoveTime = 0;

        }

        Stats& operator+=(const Stats& other) {
            mapOpsDone += other.mapOpsDone;
            duration += other.duration;
            mapReadsDone += other.mapReadsDone;
            mapWritesDone += other.mapWritesDone;
            mapInsertsDone += other.mapInsertsDone;
            totInsertTime += other.totInsertTime;
            mapRemovesDone += other.mapRemovesDone;
            totRemoveTime += other.totRemoveTime;
            return *this;
        }

        bool operator<(const Stats& other) const {
            return duration < other.duration;
        }
    };

    Stats m_stats;

    ThreadState(SharedState* shared, ureg threadIndex, u32 rangeLo, u32 rangeHi)
        : m_shared(shared), m_threadCtx(shared->adapter, threadIndex) {
        m_threadIndex = threadIndex;
        m_rangeLo = rangeLo;
        m_rangeHi = rangeHi;
        m_addIndex = rangeLo;
        m_removeIndex = rangeLo;
    }

    void registerThread() {
        m_threadCtx.registerThread();
    }

    void unregisterThread() {
        m_threadCtx.unregisterThread();
    }

    void initialPopulate() {
        TURF_ASSERT(m_addIndex == m_removeIndex);
        MapAdapter::Map* map = m_shared->map;
        for (ureg i = 0; i < m_shared->numKeysPerThread; i++) {
            u32 key = m_addIndex * Prime;
            if (key >= 2)
                map->assign(key, (void*) uptr(key));
            if (++m_addIndex == m_rangeHi)
                m_addIndex = m_rangeLo;
        }
    }

    void run() {
        MapAdapter::Map* map = m_shared->map;
        turf::CPUTimer::Converter converter;
        Stats stats;
        ureg lookupIndex = m_rangeLo;
        ureg remaining = m_shared->itersPerChunk;
        if (m_threadIndex == 0)
            m_shared->spinKicker.kick(m_shared->numThreads - 1);
        else {
            remaining = ~u32(0);
            m_shared->spinKicker.waitForKick();
        }

        // ---------
        //std::cout << "\nSTART\n";
        turf::CPUTimer::Point insertStart;
        turf::CPUTimer::Point insertEnd;
        turf::CPUTimer::Point removeStart;
        turf::CPUTimer::Point removeEnd;
        int totalWrites = m_shared->itersPerChunk * m_shared->writesPerRead * 2;
        double insertPercent = double(m_shared->insertsPerRemove) / (double(m_shared->removesPerInsert) + double(m_shared->insertsPerRemove));
        double numInsertsD = insertPercent * double(totalWrites);
        int numInserts = int(numInsertsD);
        int numRemoves = totalWrites - numInserts;
        int extraInserts = numInserts - (m_shared->itersPerChunk * m_shared->writesPerRead);
        double extraInsertsPercent = double(extraInserts) / double(totalWrites);

        turf::CPUTimer::Point start = turf::CPUTimer::get();
        u32 key;
        int counter = 0;
        int extraInsertCounter = 0;
        for (; remaining > 0; remaining--) {
            //key = 0;
            // Add
            // Check if time to be done
            // it stalls with both add and remove in loop, but not with onhly one, why
            if (m_shared->doneFlag.load(turf::Relaxed)) {
                    break;
            }

            //do extra inserts to keep r/w ratio constant
            if (extraInsertCounter < extraInserts) {
                  numInserts = m_shared->writesPerRead * 2;
                  extraInsertCounter += m_shared->writesPerRead;
            } else { 
                  numInserts = m_shared->writesPerRead;
            }

            for (ureg a = 0; a < numInserts; a++) {
                
                u32 key = m_addIndex * Prime;
            //Add key to map
                if (key >= 2) {
                    insertStart = turf::CPUTimer::get();
                    map->assign(key, (void*) uptr(key));
                    insertEnd = turf::CPUTimer::get();
                    stats.mapOpsDone++;
                    stats.mapWritesDone++;
                    stats.mapInsertsDone++;
                    //std::cout << insertEnd - insertStart;
                    stats.totInsertTime += (insertEnd - insertStart);
                } 
                //Stay within this chunk (or something) index
                if (++m_addIndex == m_rangeHi) {
                    m_addIndex = m_rangeLo;
                }
            }
            // Lookup
            if (s32(lookupIndex - m_removeIndex) < 0)
                lookupIndex = m_removeIndex;
            for (ureg l = 0; l < m_shared->readsPerWrite; l++) {
                if (m_shared->doneFlag.load(turf::Relaxed))
                    break;
                key = lookupIndex * Prime;
                //std::cout << "key: " << key << "\n";
                if (key >= 2) {
                    volatile void* value = map->get(key);
                    TURF_UNUSED(value);
                    stats.mapOpsDone++;
                    stats.mapReadsDone++;
                }
                if (++lookupIndex == m_rangeHi)
                    lookupIndex = m_rangeLo;
                if (lookupIndex == m_addIndex)
                    lookupIndex = m_removeIndex;
            }

            if (stats.mapRemovesDone < numRemoves) {
            
                for (ureg b = 0; b < m_shared->writesPerRead; b++) {

                // Remove
                    if (m_shared->doneFlag.load(turf::Relaxed))
                        break;
                    key = m_removeIndex * Prime;
                    if (key >= 2) {
                        removeStart = turf::CPUTimer::get();
                        map->erase(key);
                        removeEnd = turf::CPUTimer::get();
                        stats.mapOpsDone++;
                        stats.mapWritesDone++;
                        stats.mapRemovesDone++;
                        stats.totRemoveTime += (removeEnd - removeStart);
                    }
                    if (++m_removeIndex == m_rangeHi)
                        m_removeIndex = m_rangeLo;
                }
            }
            // Lookup
            if (s32(lookupIndex - m_removeIndex) < 0)
                lookupIndex = m_removeIndex;
            for (ureg l = 0; l < m_shared->readsPerWrite; l++) {
                if (m_shared->doneFlag.load(turf::Relaxed))
                    break;
                key = lookupIndex * Prime;
                if (key >= 2) {
                    volatile void* value = map->get(key);
                    TURF_UNUSED(value);
                    stats.mapOpsDone++;
                    stats.mapReadsDone++;
                }
                if (++lookupIndex == m_rangeHi)
                    lookupIndex = m_rangeLo;
                if (lookupIndex == m_addIndex)
                    lookupIndex = m_removeIndex;
            }
            counter++;
        }
        if (m_threadIndex == 0)
            m_shared->doneFlag.store(1, turf::Relaxed);
        m_threadCtx.update();
        turf::CPUTimer::Point end = turf::CPUTimer::get();
        //std::cout << "\nEND\n";
        // ---------

        stats.duration = converter.toSeconds(end - start);
        m_stats = stats;
    }
};

static const turf::extra::Option Options[] = {
    {"readsPerWrite", 'r', true, "number of reads per write"},
    {"itersPerChunk", 'i', true, "number of iterations per chunk"},
    {"chunks", 'c', true, "number of chunks to execute"},
    {"keepChunkFraction", 'k', true, "threshold fraction of chunk timings to keep"},
    {"writesPerRead", 'w', true, "number of writes per read"},
    {"insertsPerRemove", 'n', true, "number of inserts per remove"},
    {"removesPerInsert", 'd', true, "number of removes per insert"},
};
//TODO add option to control ratio of put vs remove

int main(int argc, const char** argv) {
    //info to record in files: options are D for default option, 
    //I for recording insert and remove times
    char print_info = 'D';
    turf::extra::Options options(Options, TURF_STATIC_ARRAY_SIZE(Options));
    options.parse(argc, argv);
    ureg itersPerChunk = options.getInteger("itersPerChunk", DefaultItersPerChunk);
    ureg chunks = options.getInteger("chunks", DefaultChunks);
    ureg readsPerWrite = options.getInteger("readsPerWrite", DefaultReadsPerWrite);
    ureg writesPerRead = options.getInteger("writesPerRead", DefaultWritesPerRead);
    ureg insertsPerRemove = options.getInteger("insertsPerRemove", DefaultInsertsPerRemove);
    ureg removesPerInsert = options.getInteger("removesPerInsert", DefaultRemovesPerInsert);
    double keepChunkFraction = options.getDouble("keepChunkFraction", 1.0);
    if (insertsPerRemove < removesPerInsert) {
        printf("\nCan't have more removes than inserts\n");
        TURF_ASSERT(false);
    }

    turf::extra::JobDispatcher dispatcher;
    ureg numCores = dispatcher.getNumPhysicalCores();
    TURF_ASSERT(numCores > 0);
    MapAdapter adapter(numCores);

    // Create shared state and register first thread
    SharedState shared(adapter, NumKeysPerThread, readsPerWrite, writesPerRead, itersPerChunk, insertsPerRemove, removesPerInsert);
    std::vector<ThreadState> threads;
    threads.reserve(numCores);
    for (ureg t = 0; t < numCores; t++) {
        u32 rangeLo = 0xffffffffu / numCores * t + 1;
        u32 rangeHi = 0xffffffffu / numCores * (t + 1) + 1;
        threads.push_back(ThreadState(&shared, t, rangeLo, rangeHi));
    }
    dispatcher.kickOne(0, &ThreadState::registerThread, threads[0]);

    {
        // Create the map and populate it entirely from main thread
        MapAdapter::Map map(MapAdapter::getInitialCapacity(numCores * NumKeysPerThread));
        shared.map = &map;
        for (ureg t = 0; t < numCores; t++) {
            threads[t].initialPopulate();
        }

        printf("{\n");
        printf("'mapType': '%s',\n", MapAdapter::getMapName());
        printf("'population': %d,\n", (int) (numCores * NumKeysPerThread));
        printf("'readsPerWrite': %d,\n", (int) readsPerWrite);
        printf("'writesPerRead': %d, \n", (int) writesPerRead);
        printf("'insertsPerRemove': %d, \n", (int) insertsPerRemove);
        printf("'removesPerInsert': %d, \n", (int) removesPerInsert);
        printf("'itersPerChunk': %d,\n", (int) itersPerChunk);
        printf("'chunks': %d,\n", (int) chunks);
        printf("'keepChunkFraction': %f,\n", keepChunkFraction);
        printf("'labels': ('numThreads', 'mapOpsDone', 'totalTime'),\n"), printf("'points': [\n");
        //printf("'labels': ('numThreads', 'mapOpsDone', 'mapReadsDone', 'mapWritesDone', 'mapInsertsDone', 'mapRemovesDone' 'I:R ratio', 'R:W ratio'),\n"), printf("'points': [\n");
        for (shared.numThreads = 1; shared.numThreads <= numCores; shared.numThreads++) {
            if (shared.numThreads > 1) {
                // Spawn and register a new thread
                dispatcher.kickOne(shared.numThreads - 1, &ThreadState::registerThread, threads[shared.numThreads - 1]);

            }

            std::vector<ThreadState::Stats> kickTotals;
            for (ureg c = 0; c < chunks; c++) {
                shared.doneFlag.storeNonatomic(false);
                dispatcher.kickMulti(&ThreadState::run, &threads[0], shared.numThreads);

                ThreadState::Stats kickTotal;
                for (ureg t = 0; t < shared.numThreads; t++)
                    kickTotal += threads[t].m_stats;
                kickTotals.push_back(kickTotal);
            }

            std::sort(kickTotals.begin(), kickTotals.end());
            ThreadState::Stats totals;
            for (ureg t = 0; t < ureg(kickTotals.size() * keepChunkFraction); t++) {
                totals += kickTotals[t];
            }
            if (print_info == 'D') { 
                printf("    (%d, %d, %f),\n", int(shared.numThreads), int(totals.mapOpsDone), totals.duration);
                //printf("    (%d, %d, %d, %d, %d, %d, %f, %f),\n", int(shared.numThreads), int(totals.mapOpsDone), int(totals.mapReadsDone), int(totals.mapWritesDone), int(totals.mapInsertsDone), int(totals.mapRemovesDone), double(totals.mapInsertsDone) / double(totals.mapRemovesDone), double(totals.mapReadsDone) / double(totals.mapWritesDone));
              

            }
            else if (print_info == 'I') {
                printf("    (%d, %f, %f),\n", int(shared.numThreads), totals.totInsertTime / totals.mapInsertsDone / 1000, totals.totRemoveTime / totals.mapRemovesDone / 1000);
            }
        }
                //printf("    (%d, %d, %f, reads %d, writes %d),\n", int(shared.numThreads), int(totals.mapOpsDone), totals.duration, int(totals.mapReadsDone), int(totals.mapWritesDone));
        printf("],\n");
        printf("}\n");

        shared.map = NULL;
    }

    dispatcher.kickMulti(&ThreadState::unregisterThread, &threads[0], threads.size());
    return 0;
}
