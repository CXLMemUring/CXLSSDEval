#ifndef CXL_DEVICE_IMPL_HPP
#define CXL_DEVICE_IMPL_HPP

#include "cxl_ssd_common.hpp"
#include "cxl_logger.hpp"
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <linux/nvme_ioctl.h>
#include <sys/ioctl.h>

namespace cxl_ssd {

// DevDax implementation for CXL device access
class DevDaxDevice : public CXLDevice {
private:
    int fd_;
    std::string device_path_;
    void* mapped_memory_;
    size_t mapped_size_;
    CXLCapabilities capabilities_;
    
public:
    DevDaxDevice() : fd_(-1), mapped_memory_(nullptr), mapped_size_(0) {}
    ~DevDaxDevice() override { close(); }
    
    bool open(const std::string& device_path) override {
        if (fd_ >= 0) {
            close();
        }
        
        // Open devdax device (e.g., /dev/dax0.0)
        fd_ = ::open(device_path.c_str(), O_RDWR);
        if (fd_ < 0) {
            CXL_LOG_ERROR("Failed to open devdax device: " + device_path);
            return false;
        }
        
        device_path_ = device_path;
        
        // Get device size
        off_t size = lseek(fd_, 0, SEEK_END);
        if (size < 0) {
            ::close(fd_);
            fd_ = -1;
            return false;
        }
        lseek(fd_, 0, SEEK_SET);
        
        // Map the entire device into memory
        mapped_memory_ = mmap(nullptr, size, PROT_READ | PROT_WRITE, 
                             MAP_SHARED, fd_, 0);
        if (mapped_memory_ == MAP_FAILED) {
            CXL_LOG_ERROR("Failed to mmap devdax device");
            ::close(fd_);
            fd_ = -1;
            mapped_memory_ = nullptr;
            return false;
        }
        
        mapped_size_ = size;
        
        // Initialize capabilities for devdax device
        capabilities_.supports_pmr = true;
        capabilities_.supports_cmb = false;
        capabilities_.supports_compression = false;
        capabilities_.supports_mwait = true;
        capabilities_.cxl_version = 0x30; // CXL 3.0
        capabilities_.pmr_size = mapped_size_;
        capabilities_.cmb_size = 0;
        
        CXL_LOG_INFO("Successfully opened devdax device: " + device_path);
        return true;
    }
    
    void close() override {
        if (mapped_memory_ && mapped_memory_ != MAP_FAILED) {
            munmap(mapped_memory_, mapped_size_);
            mapped_memory_ = nullptr;
        }
        if (fd_ >= 0) {
            ::close(fd_);
            fd_ = -1;
        }
        device_path_.clear();
    }
    
    CXLCapabilities get_capabilities() const override {
        return capabilities_;
    }
    
    std::string get_name() const override {
        return device_path_;
    }
    
    bool is_open() const override {
        return fd_ >= 0;
    }
    
    // Additional methods specific to devdax
    void* get_mapped_memory() const {
        return mapped_memory_;
    }
    
    size_t get_mapped_size() const {
        return mapped_size_;
    }
    
    // Direct memory access operations
    bool read_direct(void* buffer, size_t offset, size_t size) {
        if (!mapped_memory_ || offset + size > mapped_size_) {
            return false;
        }
        memcpy(buffer, static_cast<char*>(mapped_memory_) + offset, size);
        return true;
    }
    
    bool write_direct(const void* buffer, size_t offset, size_t size) {
        if (!mapped_memory_ || offset + size > mapped_size_) {
            return false;
        }
        memcpy(static_cast<char*>(mapped_memory_) + offset, buffer, size);
        return true;
    }
};

// NVMe implementation for CXL device access
class NVMeDevice : public CXLDevice {
private:
    // NVMe identify controller structure (simplified)
    struct nvme_id_ctrl {
        uint16_t vid;
        uint16_t ssvid;
        char sn[20];
        char mn[40];
        char fr[8];
        uint8_t rab;
        uint8_t ieee[3];
        uint8_t cmic;
        uint8_t mdts;
        uint16_t cntlid;
        uint32_t ver;
        uint32_t rtd3r;
        uint32_t rtd3e;
        uint32_t oaes;
        uint32_t ctratt;
        uint8_t rsvd100[156];
        uint16_t oacs;
        uint8_t acl;
        uint8_t aerl;
        uint8_t frmw;
        uint8_t lpa;
        uint8_t elpe;
        uint8_t npss;
        uint8_t avscc;
        uint8_t apsta;
        uint16_t wctemp;
        uint16_t cctemp;
        uint16_t mtfa;
        uint32_t hmpre;
        uint32_t hmmin;
        uint8_t tnvmcap[16];
        uint8_t unvmcap[16];
        uint32_t rpmbs;
        uint16_t edstt;
        uint8_t dsto;
        uint8_t fwug;
        uint16_t kas;
        uint16_t hctma;
        uint16_t mntmt;
        uint16_t mxtmt;
        uint32_t sanicap;
        uint32_t hmminds;
        uint16_t hmmaxd;
        uint8_t rsvd338[4];
        uint8_t anatt;
        uint8_t anacap;
        uint32_t anagrpmax;
        uint32_t nanagrpid;
        uint8_t rsvd352[160];
        uint8_t sqes;
        uint8_t cqes;
        uint16_t maxcmd;
        uint32_t nn;
        uint16_t oncs;
        uint16_t fuses;
        uint8_t fna;
        uint8_t vwc;
        uint16_t awun;
        uint16_t awupf;
        uint8_t nvscc;
        uint8_t nwpc;
        uint16_t acwu;
        uint8_t rsvd534[2];
        uint32_t sgls;
        uint32_t mnan;
        uint8_t rsvd544[224];
        char subnqn[256];
        uint8_t rsvd1024[768];
        uint32_t pmrctl;
        uint32_t pmrsts;
        uint32_t pmrebs;
        uint32_t pmrswtp;
        uint32_t pmrmscl;
        uint32_t pmrmscu;
        uint8_t rsvd1800[200];
        uint32_t cmbsz;
        uint8_t rsvd2004[3068];
    };
    
    // NVMe identify namespace structure (simplified)
    struct nvme_id_ns {
        uint64_t nsze;
        uint64_t ncap;
        uint64_t nuse;
        uint8_t nsfeat;
        uint8_t nlbaf;
        uint8_t flbas;
        uint8_t mc;
        uint8_t dpc;
        uint8_t dps;
        uint8_t nmic;
        uint8_t rescap;
        uint8_t fpi;
        uint8_t dlfeat;
        uint16_t nawun;
        uint16_t nawupf;
        uint16_t nacwu;
        uint16_t nabsn;
        uint16_t nabo;
        uint16_t nabspf;
        uint16_t noiob;
        uint8_t nvmcap[16];
        uint16_t npwg;
        uint16_t npwa;
        uint16_t npdg;
        uint16_t npda;
        uint16_t nows;
        uint8_t rsvd74[18];
        uint32_t anagrpid;
        uint8_t rsvd96[3];
        uint8_t nsattr;
        uint16_t nvmsetid;
        uint16_t endgid;
        uint8_t nguid[16];
        uint8_t eui64[8];
        struct {
            uint16_t ms;
            uint8_t ds;
            uint8_t rp;
        } lbaf[16];
        uint8_t rsvd192[192];
        uint8_t vs[3712];
    };
    
    // NVMe user I/O command structure
    struct nvme_user_io {
        uint8_t opcode;
        uint8_t flags;
        uint16_t control;
        uint16_t nblocks;
        uint16_t rsvd;
        uint64_t metadata;
        uint64_t addr;
        uint64_t slba;
        uint32_t dsmgmt;
        uint32_t reftag;
        uint16_t apptag;
        uint16_t appmask;
    };
    
    // NVMe admin command structure
    struct nvme_admin_cmd {
        uint8_t opcode;
        uint8_t flags;
        uint16_t rsvd1;
        uint32_t nsid;
        uint32_t cdw2;
        uint32_t cdw3;
        uint64_t metadata;
        uint64_t addr;
        uint32_t metadata_len;
        uint32_t data_len;
        uint32_t cdw10;
        uint32_t cdw11;
        uint32_t cdw12;
        uint32_t cdw13;
        uint32_t cdw14;
        uint32_t cdw15;
        uint32_t timeout_ms;
        uint32_t result;
    };

    int fd_;
    std::string device_path_;
    CXLCapabilities capabilities_;
    struct nvme_id_ctrl ctrl_id_;
    struct nvme_id_ns ns_id_;
    uint32_t nsid_;
    
public:
    NVMeDevice() : fd_(-1), nsid_(1) {
        memset(&ctrl_id_, 0, sizeof(ctrl_id_));
        memset(&ns_id_, 0, sizeof(ns_id_));
    }
    ~NVMeDevice() override { close(); }
    
    bool open(const std::string& device_path) override {
        if (fd_ >= 0) {
            close();
        }
        
        // Open NVMe device (e.g., /dev/nvme0n1)
        fd_ = ::open(device_path.c_str(), O_RDWR);
        if (fd_ < 0) {
            CXL_LOG_ERROR("Failed to open NVMe device: " + device_path);
            return false;
        }
        
        device_path_ = device_path;
        
        // Get controller identification
        struct nvme_admin_cmd cmd = {};
        cmd.opcode = 0x06; // Identify
        cmd.nsid = 0;
        cmd.addr = (uint64_t)&ctrl_id_;
        cmd.data_len = sizeof(ctrl_id_);
        cmd.cdw10 = 1; // Controller identify
        
        if (ioctl(fd_, NVME_IOCTL_ADMIN_CMD, &cmd) < 0) {
            CXL_LOG_ERROR("Failed to identify NVMe controller");
            ::close(fd_);
            fd_ = -1;
            return false;
        }
        
        // Get namespace identification
        cmd.nsid = nsid_;
        cmd.addr = (uint64_t)&ns_id_;
        cmd.data_len = sizeof(ns_id_);
        cmd.cdw10 = 0; // Namespace identify
        
        if (ioctl(fd_, NVME_IOCTL_ADMIN_CMD, &cmd) < 0) {
            CXL_LOG_WARN("Failed to identify namespace, using defaults");
        }
        
        // Initialize capabilities based on NVMe controller features
        capabilities_.supports_pmr = (ctrl_id_.pmrctl != 0);
        capabilities_.supports_cmb = (ctrl_id_.cmbsz != 0);
        capabilities_.supports_compression = false;
        capabilities_.supports_mwait = false;
        capabilities_.cxl_version = 0x20; // CXL 2.0
        
        // Calculate PMR size if supported
        if (capabilities_.supports_pmr) {
            uint32_t pmrctl = ctrl_id_.pmrctl;
            uint32_t pmrmsc = ctrl_id_.pmrmscl;
            capabilities_.pmr_size = (uint64_t)pmrmsc * 4096; // Assuming 4KB units
        }
        
        // Calculate CMB size if supported
        if (capabilities_.supports_cmb) {
            uint64_t cmbsz = ctrl_id_.cmbsz;
            uint32_t szu = (cmbsz >> 8) & 0xF;
            uint32_t sz = cmbsz >> 12;
            uint64_t unit_size = (szu == 0) ? 4096 : (1ULL << (12 + 4 * szu));
            capabilities_.cmb_size = sz * unit_size;
        }
        
        CXL_LOG_INFO("Successfully opened NVMe device: " + device_path);
        return true;
    }
    
    void close() override {
        if (fd_ >= 0) {
            ::close(fd_);
            fd_ = -1;
        }
        device_path_.clear();
    }
    
    CXLCapabilities get_capabilities() const override {
        return capabilities_;
    }
    
    std::string get_name() const override {
        return device_path_;
    }
    
    bool is_open() const override {
        return fd_ >= 0;
    }
    
    // NVMe specific I/O operations
    bool nvme_read(void* buffer, uint64_t lba, uint32_t nlb) {
        struct nvme_user_io io = {};
        io.opcode = 0x02; // Read
        io.flags = 0;
        io.control = 0;
        io.nblocks = nlb - 1;
        io.rsvd = 0;
        io.metadata = 0;
        io.addr = (uint64_t)buffer;
        io.slba = lba;
        io.dsmgmt = 0;
        io.reftag = 0;
        io.apptag = 0;
        io.appmask = 0;
        
        return ioctl(fd_, NVME_IOCTL_SUBMIT_IO, &io) >= 0;
    }
    
    bool nvme_write(const void* buffer, uint64_t lba, uint32_t nlb) {
        struct nvme_user_io io = {};
        io.opcode = 0x01; // Write
        io.flags = 0;
        io.control = 0;
        io.nblocks = nlb - 1;
        io.rsvd = 0;
        io.metadata = 0;
        io.addr = (uint64_t)buffer;
        io.slba = lba;
        io.dsmgmt = 0;
        io.reftag = 0;
        io.apptag = 0;
        io.appmask = 0;
        
        return ioctl(fd_, NVME_IOCTL_SUBMIT_IO, &io) >= 0;
    }
    
    // Get namespace size in blocks
    uint64_t get_namespace_size() const {
        return ns_id_.nsze;
    }
    
    // Get logical block size
    uint32_t get_lba_size() const {
        if (ns_id_.flbas & 0xF < 16) {
            uint32_t lbaf_idx = ns_id_.flbas & 0xF;
            return 1 << ns_id_.lbaf[lbaf_idx].ds;
        }
        return 512; // Default to 512 bytes
    }
};

// Factory function to create appropriate device
std::unique_ptr<CXLDevice> create_cxl_device(const std::string& type) {
    if (type == "devdax") {
        return std::make_unique<DevDaxDevice>();
    } else if (type == "nvme") {
        return std::make_unique<NVMeDevice>();
    }
    return nullptr;
}

} // namespace cxl_ssd

#endif // CXL_DEVICE_IMPL_HPP