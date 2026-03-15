/**
 * index.js - Wrapper compatível com Vue 2 / Webpack 4
 */
import createNbisModule from './nbis_wsq.js';

class NbisConverter {
    constructor() {
        this.module = null;
        this.isInitialized = false;
    }

    /**
     * Inicializa o módulo WASM.
     * @param {string} wasmPath Caminho acessível via URL (ex: '/nbis_wsq.wasm')
     */
    async init(wasmPath = '/nbis_wsq.wasm') {
        if (this.isInitialized) return this.module;

        const self = this;
        return new Promise((resolve, reject) => {
            // No modo MODULARIZE, createNbisModule retorna uma Promise
            createNbisModule({
                locateFile: function(path) {
                    if (path.endsWith('.wasm')) return wasmPath;
                    return path;
                }
            }).then((instance) => {
                self.module = instance;
                self.isInitialized = true;
                console.log("NBIS WASM: Pronto para uso.");
                resolve(instance);
            }).catch((err) => {
                console.error("NBIS WASM: Erro na inicialização", err);
                reject(err);
            });
        });
    }

    /**
     * Executa a conversão de imagem.
     */
    async convert(buffer, toWsq = true, isPng = false) {
        if (!this.isInitialized) {
            throw new Error("O conversor NBIS não foi inicializado. Chame .init() primeiro.");
        }

        // Garante que o buffer de entrada é um Uint8Array
        const uint8Array = buffer instanceof Uint8Array ? buffer : new Uint8Array(buffer);
        
        // 1. Aloca memória no heap do WebAssembly para a entrada
        const inputPtr = this.module._malloc(uint8Array.length);
        
        // Escreve os dados para a memória alocada do C
        this.module.writeArrayToMemory(uint8Array, inputPtr);
        
        // 2. Aloca 4 bytes para guardar o tamanho da saída (ponteiro de inteiro)
        const sizePtr = this.module._malloc(4);
        
        try {
            const funcName = toWsq ? 'converter_para_wsq' : 'converter_de_wsq';
            
            // 3. Executa a conversão chamando a função em C
            const outPtr = this.module.ccall(
                funcName,
                'number',
                ['number', 'number', 'number', 'number'],
                [inputPtr, uint8Array.length, isPng ? 1 : 0, sizePtr]
            );

            if (outPtr === 0) {
                throw new Error("Falha interna na conversão WASM (Motor C abortou).");
            }

            // 4. Lê o tamanho do arquivo gerado
            const outSize = this.module.getValue(sizePtr, 'i32');
            
            // 5. Pega os bytes da memória do WebAssembly para o JavaScript
            const result = new Uint8Array(this.module.HEAPU8.buffer, outPtr, outSize);
            
            // Cria uma cópia independente (pois o free logo abaixo vai destruir o original)
            const finalData = new Uint8Array(result); 

            // 6. Limpa a memória do buffer de saída que foi alocada DENTRO do C
            this.module.ccall('liberar_memoria', null, ['number'], [outPtr]);

            return finalData;

        } finally {
            // 7. Sempre libera os buffers de entrada que alocamos aqui no JavaScript
            this.module._free(inputPtr);
            this.module._free(sizePtr);
        }
    }
}

// Exporta a instância para ser usada como Singleton em todo o Vue
export default new NbisConverter();