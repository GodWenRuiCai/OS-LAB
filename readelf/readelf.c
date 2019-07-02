 /* This is a simplefied ELF reader.
 * You can contact me if you find any bugs.
 *
 * Luming Wang<wlm199558@126.com>
 */

#include "kerelf.h"
#include <stdio.h>
/* Overview:
 *   Check whether it is a ELF file.
 *
 * Pre-Condition:
 *   binary must longer than 4 byte.
 *
 * Post-Condition:
 *   Return 0 if `binary` isn't an elf. Otherwise
 * return 1.
 */
int is_elf_format(u_char *binary)
{
        Elf32_Ehdr *ehdr = (Elf32_Ehdr *)binary;
        if (ehdr->e_ident[EI_MAG0] == ELFMAG0 &&
                ehdr->e_ident[EI_MAG1] == ELFMAG1 &&
                ehdr->e_ident[EI_MAG2] == ELFMAG2 &&
                ehdr->e_ident[EI_MAG3] == ELFMAG3) {
                return 1;
        }

        return 0;
}

/* Overview:
 *   read an elf format binary file. get ELF's information
 *
 * Pre-Condition:
 *   `binary` can't be NULL and `size` is the size of binary.
 *
 * Post-Condition:
 *   Return 0 if success. Otherwise return < 0.
 *   If success, output address of every section in ELF.
 */
int readelf(u_char *binary, int size)
{
        Elf32_Ehdr *ehdr = (Elf32_Ehdr *)binary;

                printf("ehdr address is %x\n",ehdr);
        int Nr;
        Elf32_Shdr *shdr = NULL;

        u_char *ptr_sh_table = binary+ehdr->e_shoff;
        Elf32_Half sh_entry_count;
        Elf32_Half sh_entry_size;


        // check whether `binary` is a ELF file.
        if (size < 4 || !is_elf_format(binary)) {
                printf("not a standard elf format\n");
                return 0;
        }

        // get section table addr, section header number and section header size.
                Nr =ehdr->e_shnum;

                if (Nr == 0) {
                        return -1;
                }
                sh_entry_count = 0;

        // for each section header, output section number and section addr.
                for(sh_entry_count = 0;sh_entry_count<Nr;sh_entry_count++) {
                        shdr = (Elf32_Shdr *)(ptr_sh_table+sh_entry_count*sizeof(Elf32_Shdr));
                        printf("%d:0x%x\n",sh_entry_count,shdr->sh_addr);
                }

        return 0;
}
