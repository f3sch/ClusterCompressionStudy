# ClusterCompressionStudy

##
```bash
 /Users/f3sch/git/alice/sw/BUILD/ClusterCompressionStudy-latest/ClusterCompressionStudy/cluster_bench $(ls /Users/f3sch/scratch/cluster_comp/test/clusters_*.bin | shuf -n 10 | paste -sd ' ' -)
```


### alidist recipes
#### zstd.sh
``` bash
package: zstd
version: "%(tag_basename)s"
tag: v1.5.7
source: https://github.com/facebook/zstd
build_requires:
- "GCC-Toolchain:(?!osx)"
- CMake
- alibuild-recipe-tools
---
#!/bin/bash -e

cmake -S "${SOURCEDIR}/build/cmake" -B "$BUILDDIR" \
    -DCMAKE_BUILD_TYPE="$CMAKE_BUILD_TYPE" \
    -DCMAKE_INSTALL_PREFIX="$INSTALLROOT" \
    -DCMAKE_INSTALL_LIBDIR=lib
cmake --build . -- ${JOBS:+-j$JOBS}
cmake --install .

mkdir -p etc/modulefiles
alibuild-generate-module >etc/modulefiles/$PKGNAME
MODULEDIR="$INSTALLROOT/etc/modulefiles"
mkdir -p "$MODULEDIR" && rsync -a --delete etc/modulefiles/ "$MODULEDIR"
```

#### ClusterCompressionStudy.sh
``` bash
package: ClusterCompressionStudy
version: main
source: https://github.com/f3sch/ClusterCompressionStudy
build_requires:
- zstd
- zlib
- lz4
- CMake
- "GCC-Toolchain:(?!osx)"
- alibuild-recipe-tools
---
#!/bin/bash -ex

cmake -S "${SOURCEDIR}" -B "$BUILDDIR" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_BUILD_TYPE="$CMAKE_BUILD_TYPE" \
    -DCMAKE_INSTALL_PREFIX="$INSTALLROOT"
cmake --build . -- ${JOBS:+-j$JOBS}
cmake --install .

# install the compilation database so that we can post-check the code
DEVEL_SOURCES="$(readlink "$SOURCEDIR" || echo $SOURCEDIR)"
if [ "$DEVEL_SOURCES" != "$SOURCEDIR" ]; then
    perl -p -i -e "s|$SOURCEDIR|$DEVEL_SOURCES|" compile_commands.json
    ln -sf "$BUILDDIR"/compile_commands.json $DEVEL_SOURCES/compile_commands.json
fi

mkdir -p etc/modulefiles
alibuild-generate-module >etc/modulefiles/$PKGNAME
MODULEDIR="$INSTALLROOT/etc/modulefiles"
mkdir -p "$MODULEDIR" && rsync -a --delete etc/modulefiles/ "$MODULEDIR"
```
