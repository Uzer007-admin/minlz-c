# minlz-c

Русский · [English](Readme.md)

Самостоятельная реализация [MinLZ](https://github.com/minio/minlz) на C11:
статическая библиотека, утилита `mz` и тесты совместимости с Go.
Реализована спецификация MinLZ v1.0 с расширениями поиска и отдельных
индексов sidecar из v1.2.0. Go нужен для сравнительных тестов и измерений;
сборка библиотеки от него не зависит.

Текущая версия библиотеки C — **1.2.3**.

## Возможности

- Сжатие и распаковка блоков на всех пяти уровнях.
- Потоковые чтение и запись с CRC32C, проверкой размера и индексами.
- Позиционирование по индексу, `ReadAt`, файловый ввод-вывод, функции
  обратного вызова и повторно используемые рабочие области.
- Поисковые таблицы и sidecar-индексы: raw, RLE, разреженные таблицы,
  Huff0/FSE; объединённые потоки и фильтры по префиксам.
- Переносимая реализация на C и выбор SSE4.2, скалярного ассемблера,
  AVX2 или AVX512 во время выполнения с учётом компилятора, процессора и ОС.
- Утилита для сжатия, распаковки, поиска строк, работы с индексами и статистикой.

## Сборка и тестирование

Нужны CMake 3.16 или новее и компилятор C11. Поддерживаются GCC, Clang
и MSVC. Python 3 позволяет запустить тесты инструмента отчётов.
Исходники энтропийного кодирования включены в проект; внешняя библиотека
zstd не требуется.

```sh
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release
cmake --build build-release --config Release --parallel
ctest --test-dir build-release -C Release --output-on-failure
```

Visual Studio помещает исполняемые файлы в `build-release/Release/`.
Генераторы с одной конфигурацией — непосредственно в `build-release/`.

| Параметр CMake | По умолчанию | Назначение |
| --- | --- | --- |
| `MINLZ_BUILD_CLI` | `ON` | Сборка `mz` |
| `MINLZ_BUILD_TESTS` | `ON` | Регистрация тестов CTest |
| `MINLZ_BUILD_BENCH` | `ON` | Простые измерения производительности |
| `MINLZ_BUILD_PERF` | `ON` | Сравнительные измерения |
| `MINLZ_ENABLE_INTRINSICS` | `ON` | Оптимизации через intrinsics |
| `MINLZ_ENABLE_INLINE_ASM` | `ON` | Ассемблерные участки для поддерживаемых компиляторов |
| `MINLZ_ENABLE_ASM` | `AUTO` | Подключение отдельных ассемблерных файлов |
| `MINLZ_ENABLE_LTO` | `ON` | Оптимизация при компоновке Release, если доступна |
| `MINLZ_ENABLE_NATIVE` | `OFF` | Оптимизация под процессор машины сборки |
| `MINLZ_GO_REPO` | пусто | Репозиторий Go для тестов совместимости |

Экспериментальный поиск L2, подсказки Linux о больших страницах рабочей
области и передача буферов индексированному читателю по умолчанию выключены.
Подробности — в [руководстве по оптимизации](docs/OPTIMIZATION.ru.md).

## Командная строка

Примеры предполагают, что после установки `mz` доступна через `PATH`.
После сборки из исходников можно вызвать `./build-release/mz`
или `./build-release/Release/mz.exe`.

```sh
mz c -2 -o output.mz input.txt
mz d -o restored.txt output.mz

# Добавить поисковые таблицы и найти строки.
mz c -search -search.len 6 -bs 1M -o indexed.mz input.log
mz search -n "request failed" indexed.mz
mz search -offsets "request failed" indexed.mz

# Создать отдельный индекс для существующего потока.
mz sidecar build -search.lens 4,6,8 -o output.mzs output.mz
mz search -sidecar output.mzs "request failed" output.mz

mz stats -json -per-block output.mz
```

Уровни: `-xfast`, `-0` (без сжатия), `-1`, `-2`, `-3`.
В C API им соответствуют константы `MINLZ_LEVEL_*`.

## Библиотека

```c
#include <minlz.h>
#include <string.h>

int main(void) {
    const uint8_t input[] = "hello, MinLZ";
    uint8_t encoded[128], decoded[sizeof(input)];
    size_t encoded_len = 0, decoded_len = 0;

    if (minlz_encode(encoded, sizeof(encoded), &encoded_len,
                     input, sizeof(input), MINLZ_LEVEL_FASTEST) != MINLZ_OK)
        return 1;
    if (minlz_decode(decoded, sizeof(decoded), &decoded_len,
                     encoded, encoded_len) != MINLZ_OK)
        return 2;
    return decoded_len != sizeof(input) ||
           memcmp(input, decoded, sizeof(input)) != 0;
}
```

Для входных данных переменного размера определяйте размер выходного
буфера через `minlz_max_encoded_len()`. При повторных операциях используйте
`minlz_workspace`. Максимальный входной блок — 8 МиБ.
Публичный API находится в [include/minlz.h](include/minlz.h).

```sh
cmake --install build-release --config Release --prefix /your/prefix
```

```cmake
find_package(minlz 1.2 CONFIG REQUIRED)
target_link_libraries(your_target PRIVATE minlz::minlz)
```

## Совместимость и ограничения

Параметр `-DMINLZ_GO_REPO=/path/to/minlz` добавляет проверки Go → C
и C → Go для блоков, потоков, поиска, sidecar и статистики.
Вспомогательная программа включена в этот репозиторий; реализация
эталонного модуля берётся из указанного репозитория Go.
Для поисковых тестов нужен API v1.2.0.

Сжатые байты могут отличаться от Go при одинаковом результате распаковки.
Скорость и размер зависят от данных, уровня сжатия, реализации и параметров
сборки. Превосходство над Go на любой нагрузке не гарантируется.

Не реализованы словари, резервная распаковка Snappy/S2, преобразование
LZ4 и режимы HTTP/tail/follow утилиты Go. Функции LZ4 возвращают
`MINLZ_UNSUPPORTED`. `MINLZ_WITH_FALLBACK` не подключает реализацию;
`MINLZ_FULL_GO_PARITY=ON` приводит к явной ошибке конфигурации.

## Документация

| Тема | Русский | English |
| --- | --- | --- |
| Разработка и соглашения API | [Руководство](docs/DEVELOPMENT.ru.md) | [Guide](docs/DEVELOPMENT.md) |
| Совместимость с Go | [Руководство](docs/COMPATIBILITY.ru.md) | [Guide](docs/COMPATIBILITY.md) |
| Поиск и sidecar | [Руководство](docs/SEARCH.ru.md) | [Guide](docs/SEARCH.md) |
| Тестирование | [Руководство](docs/TESTING.ru.md) | [Guide](docs/TESTING.md) |
| Производительность | [Руководство](docs/PERFORMANCE.ru.md) | [Guide](docs/PERFORMANCE.md) |
| Оптимизация | [Руководство](docs/OPTIMIZATION.ru.md) | [Guide](docs/OPTIMIZATION.md) |
| Выпуски | [Руководство](docs/RELEASE_CHECKLIST.ru.md) | [Guide](docs/RELEASE_CHECKLIST.md) |

Спецификации формата сохранены на исходном английском языке:
[SPEC.md](SPEC.md) и [SPEC_SEARCH.md](SPEC_SEARCH.md).
Старые эксперименты с реализацией в заголовочных файлах находятся
в `legacy/` и не участвуют в сборке и установке.

## Лицензия

[Apache-2.0](LICENSE). Включённая часть Zstandard распространяется
по её лицензии BSD. Сведения о стороннем коде и тексты лицензий:
[THIRD_PARTY_NOTICES.ru.md](THIRD_PARTY_NOTICES.ru.md).
