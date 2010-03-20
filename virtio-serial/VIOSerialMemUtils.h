#ifndef VIOSERIAL_MEM_UTILS_H
#define VIOSERIAL_MEM_UTILS_H

PVOID AllocatePhysical(ULONG uSize);
void FreePhysical(PVOID addr);
PHYSICAL_ADDRESS GetPhysicalAddress(PVOID addr);

#endif /* VIOSERIAL_MEM_UTILS_H */
