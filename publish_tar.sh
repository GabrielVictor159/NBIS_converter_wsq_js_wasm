#!/bin/ash

# Nome do arquivo de saída
OUTPUT="nbis-wsq-lib-v1.tgz"

echo "📦 Iniciando compactação da biblioteca..."

# Remove versão antiga se existir
if [ -f "$OUTPUT" ]; then
    rm "$OUTPUT"
fi

# Executa o comando tar
# c = create, v = verbose, z = gzip, f = file
tar -cvzf "$OUTPUT" \
    index.js \
    nbis_wsq.js \
    nbis_wsq.wasm \
    package.json

echo "✅ Biblioteca exportada com sucesso: $OUTPUT"
echo "💡 Para usar no Vue: npm install ./$OUTPUT"