// x550t.h
// Header file for the Intel x550T Dual Ethernet Adaptor (10GBe)

#define X550T_PHYS_ADDRESS 0x404a800000 

#define BAR0_CTRL_REG           (0x0000) 
    #define CTRL_PCIE_MASTER_DISABLE_MASK   (0x1 << 2) 
    #define CTRL_LRST_MASK          (0x1 << 3) 
    #define CTRL_RST_MASK           (0x1 << 26) 

#define BAR0_STATUS_REG         (0x0008) 
    #define STATUS_LAN_ID_MASK      (0x3 << 2) 
    #define STATUS_LINKUP_MASK      (0x1 << 7) 
    #define STATUS_NUM_VFS_MASK     (0xFF << 10) 
    #define STATUS_IOV_ACTIVE_MASK  (0x1 << 18) 
    #define STATUS_PCIE_M_ENABLE_MASK   (0x1 << 19) 
    #define STATUS_THERMAL_SENS_MASK    (0x1 << 20) 

#define BARO_CTRL_EXT_REG       (0x0018) 
    #define CTRL_EXT_PFRSTD_MASK     (0x1 << 14) 
    #define CTRL_EXT_RO_DIS_MASK     (0x1 << 17) 
    #define CTRL_EXT_EXTENDED_VLAN_MASK (0x1 << 26) 
    #define CTRL_EXT_DRV_LOAD


#define BAR3_MSIX_LOWER_ADDR_REG 