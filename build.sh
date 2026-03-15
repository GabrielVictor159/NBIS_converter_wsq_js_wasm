#!/bin/bash
set -e

# --- AJUSTE O CAMINHO DO NBIS ---
NBIS_SRC="/home/projects/nbis_v5_0_0/Rel_5.0.0" 

echo "🧹 1. Limpando ambiente..."
rm -rf build_obj nbis_wsq.js nbis_wsq.wasm
mkdir -p build_obj

echo "🔥 2. DESATIVANDO FUNÇÕES CONFLITUOSAS NO NIST..."
python3 -c "
import os

def disable_func(path, func_name):
    if not os.path.exists(path): return
    with open(path, 'r', encoding='latin-1') as f:
        content = f.read()
    # Renomeia para OLD_ para evitar conflito de símbolo duplicado
    if func_name + '(' in content and 'OLD_' + func_name not in content:
        content = content.replace(func_name + '(', 'OLD_' + func_name + '(')
        with open(path, 'w', encoding='latin-1') as f:
            f.write(content)
        print(f'✅ Desativada: {func_name} em {os.path.basename(path)}')

# Lista de funções que agora nós controlamos no conversor_wasm.c
disable_func('$NBIS_SRC/imgtools/src/lib/wsq/tableio.c', 'getc_marker_wsq')
disable_func('$NBIS_SRC/commonnbis/src/lib/ioutil/dataio.c', 'getc_byte')
disable_func('$NBIS_SRC/commonnbis/src/lib/ioutil/dataio.c', 'putc_byte')
disable_func('$NBIS_SRC/commonnbis/src/lib/ioutil/dataio.c', 'getc_ushort')
disable_func('$NBIS_SRC/commonnbis/src/lib/ioutil/dataio.c', 'putc_ushort')
disable_func('$NBIS_SRC/commonnbis/src/lib/ioutil/dataio.c', 'getc_uint')
disable_func('$NBIS_SRC/commonnbis/src/lib/ioutil/dataio.c', 'putc_uint')
"

echo "🚀 3. Compilando Objetos..."

CFLAGS="-O3 -I$NBIS_SRC/imgtools/include \
        -I$NBIS_SRC/ijg/include \
        -I$NBIS_SRC/ijg/src/lib/jpegb \
        -I$NBIS_SRC/commonnbis/include \
        -I$NBIS_SRC/an2k/include \
        -D__UNIX__ -DSTDC_HEADERS=1 \
        -fno-common -fno-strict-aliasing \
        -Wno-implicit-function-declaration -Wno-return-type"

DIRS="$NBIS_SRC/imgtools/src/lib/wsq 
      $NBIS_SRC/imgtools/src/lib/jpegl
      $NBIS_SRC/imgtools/src/lib/image
      $NBIS_SRC/imgtools/src/lib/ihead 
      $NBIS_SRC/ijg/src/lib/jpegb 
      $NBIS_SRC/commonnbis/src/lib/util 
      $NBIS_SRC/commonnbis/src/lib/ioutil 
      $NBIS_SRC/commonnbis/src/lib/fet"

EXCLUDE="ansi2knr.c coderule.c jmemansi.c jmemdos.c jmemmac.c jmemname.c \
         cjpeg.c djpeg.c jpegtran.c rdjpgcom.c wrjpgcom.c example.c cdjpeg.c \
         rdppm.c wrppm.c rdbmp.c wrbmp.c rdtarga.c wrtarga.c rdgif.c wrgif.c \
         imgtype.c setup.c"

for dir in $DIRS; do
    if [ ! -d "$dir" ]; then continue; fi
    for f in $dir/*.c; do
        filename=$(basename "$f")
        if echo "$EXCLUDE" | grep -q "$filename"; then continue; fi
        objname=$(echo "$f" | sed "s|/|_|g").o
        python3 /home/projects/emsdk/upstream/emscripten/emcc.py "$f" -c -o "build_obj/$objname" $CFLAGS
    done
done

python3 /home/projects/emsdk/upstream/emscripten/emcc.py conversor_wasm.c -c -o build_obj/conversor_wasm.o $CFLAGS

echo "🔗 4. Linkando tudo em nbis_wsq.js..."
python3 /home/projects/emsdk/upstream/emscripten/emcc.py build_obj/*.o \
    -o nbis_wsq.js \
    -O3 \
    -s WASM=1 \
    -s ENVIRONMENT='web' \
    -s ALLOW_MEMORY_GROWTH=1 \
    -s MODULARIZE=1 \
    -s EXPORT_NAME='createNbisModule' \
    --no-entry \
    -s EXPORTED_RUNTIME_METHODS='["ccall", "cwrap", "getValue", "writeArrayToMemory", "HEAPU8"]' \
    -s EXPORTED_FUNCTIONS='["_malloc", "_free", "_converter_para_wsq", "_converter_de_wsq", "_liberar_memoria"]' \
    -s USE_LIBPNG=1 -s USE_ZLIB=1

if [ -f nbis_wsq.js ]; then
    sed -i 's/import.meta.url/""/g' nbis_wsq.js
    echo "✅ SUCESSO! O binário nbis_wsq.js foi gerado sem conflitos."
fi