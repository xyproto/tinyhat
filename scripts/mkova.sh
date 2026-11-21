#!/bin/sh
set -eu
top=$(dirname "$(dirname "$(readlink -f "$0")")")
img=$1
out=$2
ostype=$3
version=$4
size=$5
[ -f "$img" ] && [ -n "$out" ] || exit 2
epoch=${SOURCE_DATE_EPOCH:-$(git -C "$top" log -1 --format=%ct 2> /dev/null || echo 315532800)}

base=$(basename "$img" .img)
name="Tiny Hat $version ($base)"
work="$out.work"
rm -rf "$work" "$out.tmp"
mkdir -p "$work"

cp --sparse=always "$img" "$work/disk.raw"
truncate -s "$size" "$work/disk.raw"
capacity=$(stat -c %s "$work/disk.raw")
"$top/scripts/x86_64-tool.sh" qemu-img convert -f raw -O vmdk -o subformat=streamOptimized "$work/disk.raw" "$work/$base-disk1.vmdk"
rm -f "$work/disk.raw"

uuid() {
  h=$(printf '%s' "$1" | sha256sum | cut -c1-32)
  printf '%s-%s-4%s-8%s-%s' "$(echo "$h" | cut -c1-8)" "$(echo "$h" | cut -c9-12)" "$(echo "$h" | cut -c14-16)" \
    "$(echo "$h" | cut -c18-20)" "$(echo "$h" | cut -c21-32)"
}
disk_uuid=$(uuid "$name disk")
vm_uuid=$(uuid "$name machine")
mac=080027$(printf '%s' "$name mac" | sha256sum | cut -c1-6 | tr a-f A-F)
stamp=$(date -u -d "@$epoch" +%Y-%m-%dT%H:%M:%SZ)

case $ostype in
  ArchLinux)
    osid=99 osdesc=Linux26 pae=true longmode=false ioapic=
    ;;
  ArchLinux_64)
    osid=100 osdesc=Linux26_64 pae=false longmode=true ioapic='
        <IOAPIC enabled="true"/>'
    ;;
  *)
    echo "mkova.sh: unknown OS type $ostype" >&2
    exit 2
    ;;
esac

cat > "$work/$base.ovf" << EOF
<?xml version="1.0"?>
<Envelope ovf:version="1.0" xml:lang="en-US" xmlns="http://schemas.dmtf.org/ovf/envelope/1" xmlns:ovf="http://schemas.dmtf.org/ovf/envelope/1" xmlns:rasd="http://schemas.dmtf.org/wbem/wscim/1/cim-schema/2/CIM_ResourceAllocationSettingData" xmlns:vssd="http://schemas.dmtf.org/wbem/wscim/1/cim-schema/2/CIM_VirtualSystemSettingData" xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance" xmlns:vbox="http://www.virtualbox.org/ovf/machine">
  <References>
    <File ovf:id="file1" ovf:href="$base-disk1.vmdk"/>
  </References>
  <DiskSection>
    <Info>List of the virtual disks used in the package</Info>
    <Disk ovf:capacity="$capacity" ovf:diskId="vmdisk1" ovf:fileRef="file1" ovf:format="http://www.vmware.com/interfaces/specifications/vmdk.html#streamOptimized" vbox:uuid="$disk_uuid"/>
  </DiskSection>
  <NetworkSection>
    <Info>Logical networks used in the package</Info>
    <Network ovf:name="NAT">
      <Description>Logical network used by this appliance.</Description>
    </Network>
  </NetworkSection>
  <VirtualSystem ovf:id="$name">
    <Info>A virtual machine</Info>
    <ProductSection>
      <Info>Meta-information about the installed software</Info>
      <Product>Tiny Hat</Product>
      <Vendor>Tiny Hat</Vendor>
      <Version>$version</Version>
      <ProductUrl>https://github.com/xyproto/tinyhat</ProductUrl>
      <VendorUrl>https://github.com/xyproto/tinyhat</VendorUrl>
    </ProductSection>
    <OperatingSystemSection ovf:id="$osid">
      <Info>The kind of installed guest operating system</Info>
      <Description>$osdesc</Description>
      <vbox:OSType ovf:required="false">$ostype</vbox:OSType>
    </OperatingSystemSection>
    <VirtualHardwareSection>
      <Info>Virtual hardware requirements for a virtual machine</Info>
      <System>
        <vssd:ElementName>Virtual Hardware Family</vssd:ElementName>
        <vssd:InstanceID>0</vssd:InstanceID>
        <vssd:VirtualSystemIdentifier>$name</vssd:VirtualSystemIdentifier>
        <vssd:VirtualSystemType>virtualbox-2.2</vssd:VirtualSystemType>
      </System>
      <Item>
        <rasd:Caption>2 virtual CPUs</rasd:Caption>
        <rasd:Description>Number of virtual CPUs</rasd:Description>
        <rasd:ElementName>2 virtual CPUs</rasd:ElementName>
        <rasd:InstanceID>1</rasd:InstanceID>
        <rasd:ResourceType>3</rasd:ResourceType>
        <rasd:VirtualQuantity>2</rasd:VirtualQuantity>
      </Item>
      <Item>
        <rasd:AllocationUnits>MegaBytes</rasd:AllocationUnits>
        <rasd:Caption>4096 MB of memory</rasd:Caption>
        <rasd:Description>Memory Size</rasd:Description>
        <rasd:ElementName>4096 MB of memory</rasd:ElementName>
        <rasd:InstanceID>2</rasd:InstanceID>
        <rasd:ResourceType>4</rasd:ResourceType>
        <rasd:VirtualQuantity>4096</rasd:VirtualQuantity>
      </Item>
      <Item>
        <rasd:Address>0</rasd:Address>
        <rasd:Caption>sataController0</rasd:Caption>
        <rasd:Description>SATA Controller</rasd:Description>
        <rasd:ElementName>sataController0</rasd:ElementName>
        <rasd:InstanceID>3</rasd:InstanceID>
        <rasd:ResourceSubType>AHCI</rasd:ResourceSubType>
        <rasd:ResourceType>20</rasd:ResourceType>
      </Item>
      <Item>
        <rasd:Address>0</rasd:Address>
        <rasd:Caption>usb</rasd:Caption>
        <rasd:Description>USB Controller</rasd:Description>
        <rasd:ElementName>usb</rasd:ElementName>
        <rasd:InstanceID>4</rasd:InstanceID>
        <rasd:ResourceType>23</rasd:ResourceType>
      </Item>
      <Item>
        <rasd:AddressOnParent>3</rasd:AddressOnParent>
        <rasd:AutomaticAllocation>false</rasd:AutomaticAllocation>
        <rasd:Caption>sound</rasd:Caption>
        <rasd:Description>Sound Card</rasd:Description>
        <rasd:ElementName>sound</rasd:ElementName>
        <rasd:InstanceID>5</rasd:InstanceID>
        <rasd:ResourceSubType>ensoniq1371</rasd:ResourceSubType>
        <rasd:ResourceType>35</rasd:ResourceType>
      </Item>
      <Item>
        <rasd:AddressOnParent>0</rasd:AddressOnParent>
        <rasd:Caption>disk1</rasd:Caption>
        <rasd:Description>Disk Image</rasd:Description>
        <rasd:ElementName>disk1</rasd:ElementName>
        <rasd:HostResource>ovf:/disk/vmdisk1</rasd:HostResource>
        <rasd:InstanceID>6</rasd:InstanceID>
        <rasd:Parent>3</rasd:Parent>
        <rasd:ResourceType>17</rasd:ResourceType>
      </Item>
      <Item>
        <rasd:AutomaticAllocation>true</rasd:AutomaticAllocation>
        <rasd:Caption>Ethernet adapter on 'NAT'</rasd:Caption>
        <rasd:Connection>NAT</rasd:Connection>
        <rasd:ElementName>Ethernet adapter on 'NAT'</rasd:ElementName>
        <rasd:InstanceID>7</rasd:InstanceID>
        <rasd:ResourceSubType>E1000</rasd:ResourceSubType>
        <rasd:ResourceType>10</rasd:ResourceType>
      </Item>
    </VirtualHardwareSection>
    <vbox:Machine ovf:required="false" version="1.19-linux" uuid="{$vm_uuid}" name="$name" OSType="$ostype" snapshotFolder="Snapshots" lastStateChange="$stamp">
      <ovf:Info>Complete VirtualBox machine configuration in VirtualBox format</ovf:Info>
      <Hardware>
        <Memory RAMSize="4096"/>
        <HID Pointing="USBMultiTouch"/>
        <Display controller="VMSVGA" VRAMSize="128"/>
        <Firmware/>
        <BIOS>$ioapic
          <SmbiosUuidLittleEndian enabled="true"/>
          <AutoSerialNumGen enabled="true"/>
        </BIOS>
        <USB>
          <Controllers>
            <Controller name="OHCI" type="OHCI"/>
          </Controllers>
        </USB>
        <Network>
          <Adapter slot="0" enabled="true" MACAddress="$mac" type="82540EM">
            <NAT/>
          </Adapter>
        </Network>
        <AudioAdapter controller="HDA" useDefault="true" enabled="true" enabledOut="true"/>
        <Clipboard mode="Bidirectional"/>
        <StorageControllers>
          <StorageController name="SATA" type="AHCI" PortCount="1" useHostIOCache="false" Bootable="true" IDE0MasterEmulationPort="0" IDE0SlaveEmulationPort="1" IDE1MasterEmulationPort="2" IDE1SlaveEmulationPort="3">
            <AttachedDevice type="HardDisk" hotpluggable="false" port="0" device="0">
              <Image uuid="{$disk_uuid}"/>
            </AttachedDevice>
          </StorageController>
        </StorageControllers>
        <RTC localOrUTC="UTC"/>
        <CPU count="2">
          <HardwareVirtExLargePages enabled="false"/>
          <PAE enabled="$pae"/>
          <LongMode enabled="$longmode"/>
          <X2APIC enabled="true"/>
        </CPU>
      </Hardware>
    </vbox:Machine>
  </VirtualSystem>
</Envelope>
EOF

(cd "$work" && for f in "$base.ovf" "$base-disk1.vmdk"; do
  printf 'SHA256 (%s) = %s\n' "$f" "$(sha256sum "$f" | cut -d' ' -f1)"
done) > "$work/$base.mf"
touch -d "@$epoch" "$work/$base.ovf" "$work/$base.mf" "$work/$base-disk1.vmdk"
tar --format=ustar --owner=0 --group=0 --numeric-owner --mode=a+r,u+w,go-w -C "$work" \
  -cf "$out.tmp" "$base.ovf" "$base.mf" "$base-disk1.vmdk"
mv "$out.tmp" "$out"
rm -rf "$work"
