#pragma once

#include <Windows.h>
#include <d3d11.h>
#include <wrl/client.h>

#include <memory>
#include <optional>
#include <string>
#include <vector>

// Lo que se ve en la tercera columna cuando el cursor esta sobre un archivo. Es la misma
// estructura que produce el hilo de trabajo y la que guarda la cache: el hilo rellena
// `pixels`, el de UI los convierte en `texture` y los suelta.
struct Preview {
    enum class Kind {
        Empty,
        Image,
        Text,  // texto o codigo, ya en UTF-8 y recortado
        Info,  // tamano, fecha, atributos y tipo
    };

    std::wstring path;
    unsigned long long gen = 0;  // generacion: un resultado de otra seleccion no se muestra
    FILETIME modified{};         // la cache falla si el archivo cambio de fecha
    Kind kind = Kind::Empty;

    int width = 0;
    int height = 0;
    int targetPx = 0;        // lado mayor del panel con el que se decodifico
    bool downscaled = false; // el original era mas grande: agrandar la ventana merece rehacerlo
    std::vector<unsigned char> pixels;  // BGRA de alfa recto; vacio tras crear la textura
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> texture;  // solo el hilo de UI la toca

    std::string text;

    size_t Bytes() const;  // lo que ocupa segun la cache
};

// Inmutable y compartida: mover una preview entre la cache y la pantalla es mover un puntero.
using PreviewPtr = std::shared_ptr<const Preview>;

// Bloqueante, y con COM ya inicializado por quien llama: hilo de trabajo, nunca el de UI.
// targetPx es el lado mayor del panel en pixeles fisicos. Nunca falla: si no es imagen ni
// texto devuelve los metadatos.
Preview LoadPreview(std::wstring path, FILETIME modified, int targetPx);

// Expuesto solo para tests/preview_check.cpp: nullopt = esto no parece texto.
std::optional<std::string> DecodeTextPreview(const unsigned char* data, size_t size);
