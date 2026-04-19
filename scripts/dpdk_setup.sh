#!/bin/bash
#
# DPDK one-time setup script for AWS EC2 c5n instances running Ubuntu 22.04.
#
# WHAT THIS DOES:
#   - Installs build tools and DPDK dependencies
#   - Configures 2GB of 2MB hugepages (persistent across reboot)
#   - Loads vfio-pci kernel module in no-IOMMU mode (persistent)
#   - Downloads and builds DPDK 23.11.3 LTS from source
#   - Installs the MAC-aware bind/unbind helper scripts
#
# WHAT THIS DOES NOT DO:
#   - Does NOT attach ENIs (do that in the AWS console)
#   - Does NOT bind any ENI to vfio-pci (run dpdk-bind-udp.sh after setting UDP_MAC)
#   - Does NOT persist vfio-pci bindings across reboot (by design, for safety)
#
# USAGE:
#   1. Launch an Ubuntu 22.04 c5n instance with ONE primary ENI
#   2. SSH in as 'ubuntu'
#   3. Copy this script to the instance and run:
#      chmod +x setup-dpdk.sh && sudo ./setup-dpdk.sh
#   4. After it finishes, attach your second ENI from the AWS console
#   5. Edit /usr/local/bin/dpdk-bind-udp.sh to set UDP_MAC to the new ENI's MAC
#   6. Run: sudo /usr/local/bin/dpdk-bind-udp.sh
#
# REQUIREMENTS:
#   - Ubuntu 22.04 LTS
#   - Running as root (or via sudo)
#   - Internet access (to fetch DPDK source)

set -euo pipefail

# ====================================================================
# Configuration — edit these if needed
# ====================================================================
DPDK_VERSION="23.11.3"
DPDK_URL="https://fast.dpdk.org/rel/dpdk-${DPDK_VERSION}.tar.xz"
DPDK_SRC_DIR="/home/ubuntu/src"
HUGEPAGES_COUNT=1024                     # 1024 × 2MB = 2GB
HUGEPAGES_MOUNT="/mnt/huge"
HUGEPAGES_UID="${HUGEPAGES_UID:-${SUDO_UID:-1000}}"
HUGEPAGES_GID="${HUGEPAGES_GID:-${SUDO_GID:-1000}}"
HUGEPAGES_MODE="${HUGEPAGES_MODE:-1770}"
INSTALL_PREFIX="/usr/local"

# ====================================================================
# Helpers
# ====================================================================
log()  { echo -e "\n\033[1;34m[setup]\033[0m $*"; }
warn() { echo -e "\n\033[1;33m[warn]\033[0m  $*"; }
die()  { echo -e "\n\033[1;31m[fail]\033[0m  $*" >&2; exit 1; }

[ "$EUID" -eq 0 ] || die "This script must be run as root. Use: sudo $0"

# Sanity-check we're on Ubuntu 22.04
if ! grep -q 'Ubuntu 22.04' /etc/os-release; then
    warn "This script targets Ubuntu 22.04. Continuing anyway, but YMMV."
    sleep 3
fi

# ====================================================================
# Step 1: System update
# ====================================================================
log "Step 1/7: Updating apt package index..."
apt-get update -qq
# Don't do full upgrade here — it may change the running kernel and require a reboot
# mid-script. User should have already rebooted after a fresh launch.

# ====================================================================
# Step 2: Install dependencies
# ====================================================================
log "Step 2/7: Installing build tools and DPDK dependencies..."
DEBIAN_FRONTEND=noninteractive apt-get install -y \
    build-essential \
    meson \
    ninja-build \
    python3-pyelftools \
    python3-pip \
    pkg-config \
    libnuma-dev \
    libbsd-dev \
    libpcap-dev \
    libelf-dev \
    libjansson-dev \
    libssl-dev \
    zlib1g-dev \
    git \
    wget \
    curl \
    vim \
    htop \
    linux-headers-"$(uname -r)" \
    linux-modules-extra-"$(uname -r)"

# ====================================================================
# Step 3: Configure hugepages (persistent)
# ====================================================================
log "Step 3/7: Configuring hugepages (${HUGEPAGES_COUNT} × 2MB)..."

# Runtime allocation
echo "$HUGEPAGES_COUNT" > /sys/kernel/mm/hugepages/hugepages-2048kB/nr_hugepages

# Mount point
mkdir -p "$HUGEPAGES_MOUNT"
if ! mountpoint -q "$HUGEPAGES_MOUNT"; then
    mount -t hugetlbfs -o "uid=${HUGEPAGES_UID},gid=${HUGEPAGES_GID},mode=${HUGEPAGES_MODE}" nodev "$HUGEPAGES_MOUNT"
else
    CURRENT_FSTYPE="$(findmnt -n -o FSTYPE --target "$HUGEPAGES_MOUNT" 2>/dev/null || true)"
    [ -z "$CURRENT_FSTYPE" ] || [ "$CURRENT_FSTYPE" = "hugetlbfs" ] || die "$HUGEPAGES_MOUNT is mounted as $CURRENT_FSTYPE, expected hugetlbfs"
    mount -t hugetlbfs -o "remount,uid=${HUGEPAGES_UID},gid=${HUGEPAGES_GID},mode=${HUGEPAGES_MODE}" nodev "$HUGEPAGES_MOUNT"
fi

# Persist allocation across reboot
cat > /etc/sysctl.d/99-dpdk-hugepages.conf <<EOF
# DPDK hugepages - allocated at boot
vm.nr_hugepages = ${HUGEPAGES_COUNT}
EOF

# Persist mount across reboot
FSTAB_LINE="nodev $HUGEPAGES_MOUNT hugetlbfs defaults,uid=${HUGEPAGES_UID},gid=${HUGEPAGES_GID},mode=${HUGEPAGES_MODE} 0 0"
if grep -qE "[[:space:]]$HUGEPAGES_MOUNT[[:space:]]+hugetlbfs[[:space:]]" /etc/fstab; then
    sed -i -E "s|^nodev[[:space:]]+$HUGEPAGES_MOUNT[[:space:]]+hugetlbfs[[:space:]].*$|$FSTAB_LINE|" /etc/fstab
else
    echo "$FSTAB_LINE" >> /etc/fstab
fi

# Verify
ALLOCATED=$(cat /sys/kernel/mm/hugepages/hugepages-2048kB/nr_hugepages)
[ "$ALLOCATED" -eq "$HUGEPAGES_COUNT" ] || die "Hugepage allocation failed (got $ALLOCATED, wanted $HUGEPAGES_COUNT)"
MOUNT_UID=$(stat -c '%u' "$HUGEPAGES_MOUNT")
MOUNT_GID=$(stat -c '%g' "$HUGEPAGES_MOUNT")
MOUNT_MODE=$(stat -c '%a' "$HUGEPAGES_MOUNT")
[ "$MOUNT_UID" = "$HUGEPAGES_UID" ] || die "$HUGEPAGES_MOUNT uid is $MOUNT_UID, expected $HUGEPAGES_UID"
[ "$MOUNT_GID" = "$HUGEPAGES_GID" ] || die "$HUGEPAGES_MOUNT gid is $MOUNT_GID, expected $HUGEPAGES_GID"
[ "$MOUNT_MODE" = "$HUGEPAGES_MODE" ] || die "$HUGEPAGES_MOUNT mode is $MOUNT_MODE, expected $HUGEPAGES_MODE"
log "Hugepages configured: $ALLOCATED pages × 2MB = $((ALLOCATED * 2))MB"
log "hugetlbfs mounted at $HUGEPAGES_MOUNT with uid=${HUGEPAGES_UID}, gid=${HUGEPAGES_GID}, mode=${HUGEPAGES_MODE}"

# ====================================================================
# Step 4: Load vfio-pci in no-IOMMU mode (persistent)
# ====================================================================
log "Step 4/7: Configuring vfio-pci in no-IOMMU mode..."

modprobe vfio-pci
echo 1 > /sys/module/vfio/parameters/enable_unsafe_noiommu_mode

# Persist module load on boot
echo "vfio-pci" > /etc/modules-load.d/dpdk.conf

# Persist no-IOMMU mode on boot
cat > /etc/modprobe.d/vfio-noiommu.conf <<EOF
# Required for DPDK on AWS virtualized Nitro instances (non-metal)
options vfio enable_unsafe_noiommu_mode=1
EOF

# Verify
NOIOMMU=$(cat /sys/module/vfio/parameters/enable_unsafe_noiommu_mode)
[ "$NOIOMMU" = "Y" ] || die "vfio-pci no-IOMMU mode not enabled"
log "vfio-pci loaded and no-IOMMU mode enabled"

# ====================================================================
# Step 5: Download and build DPDK
# ====================================================================
log "Step 5/7: Downloading and building DPDK ${DPDK_VERSION}..."

# If DPDK is already installed at the target version, skip the build
if pkg-config --modversion libdpdk 2>/dev/null | grep -q "^${DPDK_VERSION}$"; then
    log "DPDK ${DPDK_VERSION} already installed — skipping build."
else
    mkdir -p "$DPDK_SRC_DIR"
    chown ubuntu:ubuntu "$DPDK_SRC_DIR"
    cd "$DPDK_SRC_DIR"

    if [ ! -d "dpdk-stable-${DPDK_VERSION}" ]; then
        log "Downloading DPDK source tarball..."
        sudo -u ubuntu wget -q --show-progress "$DPDK_URL"
        sudo -u ubuntu tar xf "dpdk-${DPDK_VERSION}.tar.xz"
    fi

    cd "dpdk-stable-${DPDK_VERSION}"

    # Clean previous build if interrupted
    if [ -d build ]; then
        log "Previous build directory found — removing for a clean build..."
        rm -rf build
    fi

    log "Running meson setup (this takes ~30 seconds)..."
    sudo -u ubuntu meson setup build \
        --prefix="$INSTALL_PREFIX" \
        -Dexamples=helloworld,l2fwd,skeleton

    log "Compiling DPDK (this takes 5-10 minutes on c5n.2xlarge)..."
    cd build
    sudo -u ubuntu ninja

    log "Installing DPDK to ${INSTALL_PREFIX}..."
    ninja install
    ldconfig

    # Copy example binaries to PATH (ninja install doesn't do this)
    log "Installing example binaries to ${INSTALL_PREFIX}/bin..."
    find examples -maxdepth 2 -name 'dpdk-*' -type f -executable -exec cp {} "${INSTALL_PREFIX}/bin/" \;
fi

# Verify install
INSTALLED_VERSION=$(pkg-config --modversion libdpdk 2>/dev/null || echo "none")
[ "$INSTALLED_VERSION" = "$DPDK_VERSION" ] || die "DPDK install verification failed (got: $INSTALLED_VERSION)"
log "DPDK ${INSTALLED_VERSION} installed successfully"

# ====================================================================
# Step 6: Install MAC-aware bind/unbind helper scripts
# ====================================================================
log "Step 6/7: Installing bind/unbind helper scripts..."

cat > /usr/local/bin/dpdk-bind-udp.sh << 'SCRIPT_END'
#!/bin/bash
#
# Binds the UDP ENI to vfio-pci based on MAC address.
# MAC is stable across stop/start; PCI address and iface name are NOT.
#
# EDIT THE UDP_MAC VALUE BELOW before running this script.
#

set -e

# ==== EDIT THIS: MAC address of your UDP ENI (from AWS console) ====
UDP_MAC="CHANGE_ME_TO_UDP_ENI_MAC"
# ===================================================================

if [ "$UDP_MAC" = "CHANGE_ME_TO_UDP_ENI_MAC" ]; then
    echo "ERROR: Edit /usr/local/bin/dpdk-bind-udp.sh and set UDP_MAC to your UDP ENI's MAC."
    echo "Find it in AWS Console: EC2 -> Network Interfaces -> (your UDP ENI) -> MAC address"
    exit 1
fi

FOUND_IFACE=""
FOUND_PCI=""
for addr_file in /sys/class/net/*/address; do
    iface=$(basename $(dirname "$addr_file"))
    [ "$iface" = "lo" ] && continue
    mac=$(cat "$addr_file")
    if [ "$mac" = "$UDP_MAC" ]; then
        FOUND_IFACE="$iface"
        FOUND_PCI=$(basename $(readlink /sys/class/net/$iface/device 2>/dev/null))
        break
    fi
done

if [ -z "$FOUND_IFACE" ]; then
    echo "No kernel interface with MAC $UDP_MAC found."
    echo "It may already be bound to vfio-pci. Current status:"
    /usr/local/bin/dpdk-devbind.py --status-dev net
    exit 0
fi

echo "Found UDP ENI: iface=$FOUND_IFACE  pci=$FOUND_PCI  mac=$UDP_MAC"

# Safety: refuse to bind if this interface has the default route
if ip route show default | grep -q "dev $FOUND_IFACE"; then
    echo "ERROR: $FOUND_IFACE holds the default route. Refusing to bind — this would kill SSH."
    exit 1
fi

modprobe vfio-pci
echo 1 > /sys/module/vfio/parameters/enable_unsafe_noiommu_mode

echo "Bringing $FOUND_IFACE down..."
ip link set "$FOUND_IFACE" down

echo "Binding $FOUND_PCI to vfio-pci..."
/usr/local/bin/dpdk-devbind.py --bind=vfio-pci "$FOUND_PCI"

echo ""
echo "Done. Current status:"
/usr/local/bin/dpdk-devbind.py --status-dev net
SCRIPT_END
chmod +x /usr/local/bin/dpdk-bind-udp.sh

cat > /usr/local/bin/dpdk-unbind-udp.sh << 'SCRIPT_END'
#!/bin/bash
#
# Unbinds UDP ENI(s) from vfio-pci and returns them to the kernel ena driver.
#

set -e

FOUND=0
for pci_dev in $(/usr/local/bin/dpdk-devbind.py --status-dev net | awk '/drv=vfio-pci/ {print $1}'); do
    echo "Unbinding $pci_dev from vfio-pci, rebinding to ena..."
    /usr/local/bin/dpdk-devbind.py --bind=ena "$pci_dev"
    FOUND=$((FOUND + 1))
done

if [ "$FOUND" -eq 0 ]; then
    echo "No device currently bound to vfio-pci. Nothing to do."
fi

echo ""
echo "Current status:"
/usr/local/bin/dpdk-devbind.py --status-dev net
SCRIPT_END
chmod +x /usr/local/bin/dpdk-unbind-udp.sh

log "Installed: /usr/local/bin/dpdk-bind-udp.sh"
log "Installed: /usr/local/bin/dpdk-unbind-udp.sh"

# ====================================================================
# Step 7: Final summary
# ====================================================================
log "Step 7/7: Setup complete!"

cat <<EOF

===============================================================================
  DPDK ${DPDK_VERSION} is installed and ready.
===============================================================================

VERIFY THE INSTALL:

  pkg-config --modversion libdpdk        # should print ${DPDK_VERSION}
  grep Huge /proc/meminfo                # should show ${HUGEPAGES_COUNT} hugepages
  dpdk-devbind.py --status-dev net       # shows your network interfaces

NEXT STEPS:

  1. Attach a second ENI to this instance from the AWS console
     (must be in the same subnet/AZ as the primary ENI).

  2. Note the new ENI's MAC address from the AWS console, then edit:
       sudo nano /usr/local/bin/dpdk-bind-udp.sh
     Change UDP_MAC to the new ENI's MAC (e.g., "02:aa:bb:cc:dd:ee").

  3. Bind the UDP ENI to vfio-pci:
       sudo /usr/local/bin/dpdk-bind-udp.sh

  4. Test with testpmd:
       sudo dpdk-testpmd -l 0-3 -n 4 -- -i --rxq=2 --txq=2

  5. Run this repo's DPDK engine the same way:
       sudo ./scripts/run_engine.sh --iova-mode=pa

  6. To unbind (return ENI to kernel):
       sudo /usr/local/bin/dpdk-unbind-udp.sh

REMEMBER: vfio-pci bindings do NOT persist across reboot. After every
stop/start or reboot, re-run dpdk-bind-udp.sh.

===============================================================================
EOF
