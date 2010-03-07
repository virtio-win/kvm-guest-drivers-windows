#ifndef VIOSERIAL_MEM_UTILS_H
#define VIOSERIAL_MEM_UTILS_H

PVOID VIOSerialAllocatePhysical(ULONG uSize);
void VIOSerialFreePhysical(PVOID addr);
PHYSICAL_ADDRESS VIOSerialGetPhysicalAddress(PVOID addr);

#endif /* VIOSERIAL_MEM_UTILS_H */
