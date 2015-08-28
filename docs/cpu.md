# CPU Documentation

## Memory Management

TODO

## References

### PowerPC

The processor in the 360 is a 64-bit PowerPC chip running with 32-bit memory
addresses. This can make some of the documentation a little confusing as most
of the PowerPC docs only have a 'you're in 32 or 64' style. The CPU is largely
similar to the PPC part in the PS3, so Cell documents often line up for the
core instructions. The 360 adds some additional AltiVec instructions, though,
which are only documented in a few places (like the gcc source code, etc).

* [Free60 Info](http://www.free60.org/Xenon_\(CPU\))
* [Power ISA docs](https://www.power.org/wp-content/uploads/2012/07/PowerISA_V2.06B_V2_PUBLIC.pdf) (aka 'PowerISA')
* [PowerPC Programming Environments Manual](https://www-01.ibm.com/chips/techlib/techlib.nsf/techdocs/F7E732FF811F783187256FDD004D3797/$file/pem_64bit_v3.0.2005jul15.pdf) (aka 'pem_64')
* [PowerPC Vector PEM](https://www-01.ibm.com/chips/techlib/techlib.nsf/techdocs/C40E4C6133B31EE8872570B500791108/$file/vector_simd_pem_v_2.07c_26Oct2006_cell.pdf)
* [AltiVec PEM](http://cache.freescale.com/files/32bit/doc/ref_manual/ALTIVECPEM.pdf)
* [VMX128 Opcodes](http://biallas.net/doc/vmx128/vmx128.txt)
* [AltiVec Decoding](https://github.com/kakaroto/ps3ida/blob/master/plugins/PPCAltivec/src/main.cpp)

### x64

* [Intel Manuals](http://www.intel.com/content/www/us/en/processors/architectures-software-developer-manuals.html)
** [Combined Intel Manuals](http://www.intel.com/content/dam/www/public/us/en/documents/manuals/64-ia-32-architectures-software-developer-manual-325462.pdf)
* [Apple AltiVec/SSE Migration Guide](https://developer.apple.com/legacy/library/documentation/Performance/Conceptual/Accelerate_sse_migration/Accelerate_sse_migration.pdf)
