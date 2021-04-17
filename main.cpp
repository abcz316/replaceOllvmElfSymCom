// main.cpp
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <elf.h>
#include <unistd.h>
#include <vector>
using namespace std;
/*
本工具用于解决ollvm编译出来的Linux驱动文件，加载进内核会报错“please compile with -fno-common”的问题
使用的前提是：需要在驱动的源码文件里面加入许多个全局的int变量定义。如：int xaa1, yaa1, xaa2, yaa2;
接下来，启动本工具： ./main [目标驱动文件路径] [全局的int变量名1] [全局的int变量名2] [全局的int变量名3]....
例如：

	//驱动文件源码内：

	int xaa1, yaa1;
	int test1(int a) __attribute__((__annotate__(("bcf fla sub bcf_loop=1000 bcf_prob=100 split_num=1000"))))
	{
		int b = a + 2;
		return b;
	}

	int xaa2, yaa2;
	int test2(int a) __attribute__((__annotate__(("bcf fla sub bcf_loop=1000 bcf_prob=100 split_num=1000"))))
	{
		int b = a + 3;
		return b;
	}

	//编译出ko驱动文件后，执行：
	./main rwProcMem37.ko xaa1 yaa1 xaa2 yaa2

*/
int main(int argc, char **argv)
{
	int fd;
	char *mod;
	unsigned int size, i, j, shn, n;
	Elf64_Sym *syms, *sym;
	vector<Elf64_Sym *> vOllvmComSym;
	vector<Elf64_Sym *> vMyFakeSym;

	Elf64_Shdr *shdrs, *shdr;
	Elf64_Ehdr *ehdr;
	const char *strtab;


	vector<char*> vMyFakeSymName;

	for (size_t i = 2; i < argc; i++)
	{
		//检查fake变量名是否有重复
		int exist_cnt = 0;
		for (char* name : vMyFakeSymName)
		{
			if (strcmp(name, argv[i]) == 0)
			{
				exist_cnt++;
			}
		}
		if (exist_cnt > 1)
		{
			printf("error! my fake name is repeated.\n");
			return 0;
		}
		vMyFakeSymName.push_back(argv[i]);
	}


	fd = open(argv[1], O_RDWR);
	size = lseek(fd, 0L, SEEK_END);
	mod = (char*)mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);

	ehdr = (Elf64_Ehdr *)mod;
	shdrs = (Elf64_Shdr *)(mod + ehdr->e_shoff);
	shn = ehdr->e_shnum == 0 ? shdrs[0].sh_size : ehdr->e_shnum;

	for (i = 0; i < shn; i++) {
		shdr = &shdrs[i];
		if (shdr->sh_type == SHT_SYMTAB || shdr->sh_type == SHT_DYNSYM) {
			syms = (Elf64_Sym *)(mod + shdr->sh_offset);
			strtab = mod + shdrs[shdr->sh_link].sh_offset;
			n = shdr->sh_size / shdr->sh_entsize;
			for (j = 0; j < n; j++) {
				char stype, sbind, sinfo;

				sym = &syms[j];
				stype = ELF64_ST_TYPE(sym->st_info);
				sbind = ELF32_ST_BIND(sym->st_info);
				sinfo = ELF32_ST_INFO(sbind, stype);
				if (stype == STT_OBJECT
					&& sbind == STB_GLOBAL
					&& sym->st_other == STV_DEFAULT) {

					char name[512] = { 0 };
					strncpy(name, strtab + sym->st_name, sizeof(n));
					name[sizeof(name) - 1] = '\0';

					if (sym->st_shndx == SHN_COMMON)
					{
						//ollvm
						if (name[0] == 'x' || name[0] == 'y') {
							if (name[1] == '\0' || name[1] == '.') {
								printf("found ollvm:%s, st_name:%d, st_value:%ld\n", name, sym->st_name, sym->st_value);
								vOllvmComSym.push_back(sym);
							}

						}
					}
					else
					{
						//my fake
						for (char* s : vMyFakeSymName)
						{
							if (strcmp(name, s) == 0)
							{
								printf("found my fake:%s, st_name:%d, st_value:%ld\n", name, sym->st_name, sym->st_value);
								vMyFakeSym.push_back(sym);
							}
						}

					}

				}

			}
		}
	}
	if (vMyFakeSymName.size() != vMyFakeSym.size())
	{
		printf("error! my fake name can't all be found.\n");
		return 0;
	}

	if (vMyFakeSymName.size() < vOllvmComSym.size())
	{
		printf("error! the count of ollvm com sym:%zd, the count of my fake name too little.\n", vOllvmComSym.size());
		return 0;
	}

	for (Elf64_Sym * ollvmSym : vOllvmComSym)
	{
		Elf64_Sym * myFakeSym = (*vMyFakeSym.rbegin());

		printf("@@@@@@@@ [old] ollvmComName:%s,\tmyFakeName:%s\n", strtab + ollvmSym->st_name, strtab + myFakeSym->st_name);

		//换shndx
		ollvmSym->st_shndx = myFakeSym->st_shndx;

		//换名字
		Elf64_Word ollvmComName = ollvmSym->st_name;
		ollvmSym->st_name = myFakeSym->st_name;

		myFakeSym->st_name = ollvmComName;

		vMyFakeSym.pop_back();

		printf("@@@@@@@@ [new] ollvmComName:%s,\tmyFakeName:%s\n", strtab + ollvmSym->st_name, strtab + myFakeSym->st_name);

	}
	munmap(mod, size);
	close(fd);
	return 0;
}