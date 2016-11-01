#include "../util/debug.hh"
#include "diskmod.hh"

DiskMod::DiskMod(const vector<DiskInfo> v_diskInfo) {

    std::set<disk_id_t> diskIdSet;

    for (DiskInfo diskInfo : v_diskInfo) {
        assert(!this->_diskInfo.count(diskInfo.diskId));
        assert(!this->_usedLBA.count(diskInfo.diskId));
        assert(!this->_diskMutex.count(diskInfo.diskId));

        this->_diskInfo[diskInfo.diskId] = diskInfo;
        this->_usedLBA[diskInfo.diskId] = new BitMap(diskInfo.numBlock);
        this->_diskMutex[diskInfo.diskId] = new mutex();
        this->_diskStatus[diskInfo.diskId] = true;
        diskIdSet.insert(diskInfo.diskId);
    }

    this->_numDisks = v_diskInfo.size();

    int numThread = ConfigMod::getInstance().getNumThread();
    this->_stp.size_controller().resize(numThread);

#ifdef DISKLBA_OUT
    fp = fopen("disklba.out", "w");
#endif /* DISKLBA_OUT */
}

DiskMod::~DiskMod() {
    for (auto diskInfo : this->_diskInfo) {
        delete this->_usedLBA[diskInfo.first];
        delete this->_diskMutex[diskInfo.first];
    }
}

#ifdef DIRECT_LBA_CONTAINER_MAPPING
disk_id_t DiskMod::getDiskByContainerId(container_id_t containerId, coding_parm_t n) {
    return (containerId == INVALID_CONTAINER)? INVALID_DISK : containerId % n;
}

lba_t DiskMod::getLBAByContainerId(container_id_t containerId, coding_parm_t n) {
    int numBlockPerContainer = ConfigMod::getInstance().getNumBlockPerContainer();
    return (containerId == INVALID_CONTAINER)? INVALID_LBA : containerId / n * numBlockPerContainer;
}
#endif // ifdef DIRECT_LBA_CONTAINER_MAPPING

lba_t DiskMod::writeContainer(disk_id_t diskId, container_id_t containerId, const unsigned char *buf, LL ts) {
    len_t numBlockPerContainer = ConfigMod::getInstance().getNumBlockPerContainer();
    len_t containerSize = ConfigMod::getInstance().getContainerSize();

    return this->writeBlocksToDisk(diskId, containerId, numBlockPerContainer, containerSize, buf, INVALID_LBA, ts);
}

lba_t DiskMod::writeContainerData(disk_id_t diskId, container_id_t containerId, const unsigned char *buf, LL ts) {
    len_t numBlockPerContainer = ConfigMod::getInstance().getNumBlockPerContainer();
    len_t containerDataSize = ConfigMod::getInstance().getContainerDataSize();

    return this->writeBlocksToDisk(diskId, containerId, numBlockPerContainer, containerDataSize, buf, INVALID_LBA, ts);
}

lba_t DiskMod::writeContainerReserved(disk_id_t diskId, lba_t containerLBA, const unsigned char *buf, LL ts) {
    len_t numBlockPerContainer = ConfigMod::getInstance().getNumBlockPerContainer();
    len_t numBlockReservedPerContainer = ConfigMod::getInstance().getNumBlockReservedPerContainer();
    len_t containerReservedSize = ConfigMod::getInstance().getContainerReservedSize();

    return this->writeBlocksToDisk(diskId, 0, numBlockReservedPerContainer, containerReservedSize, buf, containerLBA + (numBlockPerContainer - numBlockReservedPerContainer), ts);
}

lba_t DiskMod::writeBlocksToDisk(disk_id_t diskId, container_id_t containerId, lba_t numLBA, len_t writeLength, const unsigned char *buf, lba_t startingLBA, LL ts) {
    if (numLBA == 0 || writeLength <= 0 || diskId == INVALID_DISK) {
        return INVALID_LBA;
    }

    assert(this->_diskMutex.count(diskId));
    lock_guard<mutex> lk(*this->_diskMutex[diskId]);
    assert(this->_diskInfo.count(diskId));
    assert(this->_usedLBA.count(diskId));

    bool needsAlloc = (startingLBA == INVALID_LBA);
    len_t blockSize = ConfigMod::getInstance().getBlockSize();

    lba_t lba = startingLBA;
    if (needsAlloc) {
#ifdef DIRECT_LBA_CONTAINER_MAPPING
        int numBlockPerContainer = ConfigMod::getInstance().getNumBlockPerContainer();
        assert(diskId == containerId % this->_numDisks);
        lba = containerId / this->_numDisks * numBlockPerContainer;
        this->_usedLBA[diskId]->setBitRange(lba, numBlockPerContainer);
#else
        lba_t &front= this->_diskInfo[diskId].writeFront;
        lba = this->_usedLBA[diskId]->getFirstZerosAndFlip(front, numLBA);
#endif // DIRECT_LBA_CONTAINER_MAPPING
        if (lba == INVALID_LBA) {
            debug_error("Disk %d is out of space for container %d\n", diskId, containerId);
            return lba;
        }
#ifndef DIRECT_LBA_CONTAINER_MAPPING
        front += numLBA;
#endif // DIRECT_LBA_CONTAINER_MAPPING 
    }
    assert(lba != INVALID_LBA);

    offset_t diskOffset = lba * blockSize;
    debug_info("write container %d to LBA %d (disk %d, off %llu)\n", containerId, lba, diskId, diskOffset);

#ifdef ACTUAL_DISK_IO
    if (pwrite(this->_diskInfo[diskId].fd, buf, writeLength, diskOffset) != writeLength) {
        debug_error("Error on pwrite buf=%p to disk %d (fd=%d) at %llu length %lld: %s\n", buf, diskId, this->_diskInfo[diskId].fd, diskOffset, writeLength, strerror(errno));
        if (needsAlloc) {
            this->_usedLBA[diskId]->clearBitRange(lba, numLBA);
        }
        return INVALID_LBA;
    }
    debug_info("Write container %d to disk %d at %llu length %lld\n", containerId, diskId, diskOffset, writeLength);
#endif
#ifdef DISKLBA_OUT
    fprintf(fp, "%lld %d %lld %d %d\n",ts*1000, diskId, diskOffset / DISK_BLOCK_SIZE, writeLength / DISK_BLOCK_SIZE, 0);
#endif
    if (needsAlloc) {
        this->_diskInfo[diskId].remaining -= numLBA * blockSize;
    }

    return lba;
}

lba_t DiskMod::writePartialContainer(disk_id_t diskId, container_id_t containerId, lba_t startingLBA, len_t numLBA, const unsigned char *buf, bool isNewWrite, LL ts) {
    assert(this->_diskMutex.count(diskId));
    lock_guard<mutex> lk(*this->_diskMutex[diskId]);
    assert(this->_diskInfo.count(diskId));
    assert(this->_usedLBA.count(diskId));

    len_t blockSize = ConfigMod::getInstance().getBlockSize();
    // avoid disk overflow
    assert(startingLBA != INVALID_LBA && startingLBA + numLBA < this->_diskInfo[diskId].numBlock);
    // update disk usage and write to disk
    if (isNewWrite) {
        this->_usedLBA[diskId]->setBitRange(startingLBA, numLBA);
    }
    offset_t diskOffset = startingLBA * blockSize;
    len_t writeLength = numLBA * blockSize;
#ifdef ACTUAL_DISK_IO
    if (pwrite(this->_diskInfo[diskId].fd, buf, writeLength, diskOffset) != writeLength) {
        debug_error("Error on pwrite buf=%p to disk %d (fd=%d) at %llu length %lld: %s\n", buf, diskId, this->_diskInfo[diskId].fd, diskOffset, writeLength, strerror(errno));
        if (isNewWrite) {
            this->_usedLBA[diskId]->clearBitRange(startingLBA, numLBA);
        }
        return INVALID_LBA;
    }
    debug_info("Write container %d to disk %d at %llu length %lld\n", containerId, diskId, diskOffset, writeLength);
#endif
#ifdef DISKLBA_OUT
    fprintf(fp, "%lld %d %lld %d %d\n",ts*1000, diskId, diskOffset / DISK_BLOCK_SIZE, writeLength / DISK_BLOCK_SIZE, 0);
#endif
    if (isNewWrite) {
        this->_diskInfo[diskId].remaining -= numLBA * blockSize;
    }
    return startingLBA;
}

bool DiskMod::readContainer(disk_id_t diskId, lba_t containerLBA, unsigned char *buf, LL ts) {
    len_t containerSize = ConfigMod::getInstance().getContainerSize();
    container_off_len_t offLen (0, containerSize);

    return this->readPartialContainer(diskId, containerLBA, offLen, buf, ts);
}

bool DiskMod::readPartialContainer(disk_id_t diskId, lba_t containerLBA, container_off_len_t offLen, unsigned char *buf, LL ts) {
    assert(this->_diskMutex.count(diskId));
    lock_guard<mutex> lk(*this->_diskMutex[diskId]);
    assert(this->_diskInfo.count(diskId));
    assert(this->_usedLBA.count(diskId));

    len_t blockSize = ConfigMod::getInstance().getBlockSize();

    assert(offLen.first % blockSize == 0);
    assert(offLen.second % blockSize == 0);

    offset_t diskOffset = containerLBA * blockSize + offLen.first;

#ifdef ACTUAL_DISK_IO
    if (pread(this->_diskInfo[diskId].fd, buf, offLen.second, diskOffset) < 0) {
        debug_error("Error on pread buf=%p to disk %d (fd=%d) at %llu length %d: %s\n", buf, diskId, this->_diskInfo[diskId].fd, diskOffset, offLen.second, strerror(errno));
        return false;
    }
    debug_info("Read from disk %d at %llu length %u\n", diskId, diskOffset, offLen.second);
#endif

#ifdef DISKLBA_OUT
    fprintf(fp, "%lld %d %lld %d %d\n",ts*1000, diskId, diskOffset / DISK_BLOCK_SIZE, offLen.second / DISK_BLOCK_SIZE, 1);
#endif
    
    return true;
}

bool DiskMod::discardContainer(disk_id_t diskId, container_id_t containerId) {
    len_t numBlockPerContainer = ConfigMod::getInstance().getNumBlockPerContainer();
    len_t containerSize = ConfigMod::getInstance().getContainerSize();

    // Todo use trim instead of writing zeros
    unsigned char *buf = (unsigned char*) buf_malloc(containerSize);
    //    unsigned char *buf = new unsigned char[containerSize];
    memset(buf, 0, containerSize);
    lba_t lba = INVALID_LBA;
#ifdef DIRECT_LBA_CONTAINER_MAPPING
        assert(diskId == containerId % this->_numDisks);
        lba = containerId / this->_numDisks * numBlockPerContainer;
#else
        // Todo
        assert(0);
#endif // DIRECT_LBA_CONTAINER_MAPPING

    bool ret = this->writeBlocksToDisk(diskId, containerId, numBlockPerContainer, containerSize, buf, lba);

    delete [] buf;

    return ret;
}

std::vector<disk_id_t> DiskMod::selectDisks(coding_parm_t n, bool isLog, std::set<disk_id_t> filter) const {
    assert (this->_diskInfo.size() >= (unsigned int) n);

    vector<disk_id_t> selectedDisks;
#ifndef DIRECT_LBA_CONTAINER_MAPPING
    disk_id_t diskId = 0;
    for (auto &disk: this->_diskInfo) {
        // skip unmatched type of disks
        if ((!isLog && disk.second.isLog) || (isLog && !disk.second.isLog)) continue;
        // skip disk used in the same stripe
        if (filter.count(diskId)) continue;
        // stop screening if there are enough disks for the stripe
        if (selectedDisks.size() >= (unsigned int) n) break;
        selectedDisks.push_back(diskId);
    }
#endif // ifndef DIRECT_LBA_CONTAINER_MAPPING
    return selectedDisks;
}
