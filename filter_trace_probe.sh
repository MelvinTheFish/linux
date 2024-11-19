pci_probe_funcs=(
        "ncore_pci_probe"
        "pci_probe_reset_slot"
        "pci_probe_reset_bus"
        "local_pci_probe"
        "acpi_pci_probe_root_resources"
        "virtio_pci_probe"
        "platform_pci_probe.part.0"
        "platform_pci_probe"
        "usb_hcd_pci_probe"
        "ehci_pci_probe"
        "ohci_pci_probe"
        "uhci_pci_probe"
        "intel_scu_pci_probe"
        "xhci_pci_probe"
        "intel_spi_pci_probe"
        "intel_vsec_pci_probe"
        "ipmi_pci_probe"
        "mgag200_pci_probe"
        "idxd_pci_probe"
)



for func in "${pci_probe_funcs[@]}"; do
        echo $func | sudo tee -a /sys/kernel/tracing/set_ftrace_filter
done
