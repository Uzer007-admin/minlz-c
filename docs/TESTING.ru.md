# Тестирование

Русский · [English](TESTING.md) · [На главную](../Readme.ru.md)

## Сборка Release

```sh
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release
cmake --build build-release --config Release --parallel
ctest --test-dir build-release -C Release --output-on-failure
```

CTest включает основные модульные тесты, принудительные реализации
portable/SSE4.2/asm/AVX2, пробу AVX512, проверки выделения памяти рабочих
областей и тесты инструмента отчётов при наличии Python. Неподдерживаемые
режимы используют безопасную замену; успешная проба сама по себе
не доказывает выполнение инструкций AVX512.

## Эталон Go

```sh
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release \
  -DMINLZ_GO_REPO=/path/to/minlz
cmake --build build-release --config Release --parallel
ctest --test-dir build-release -C Release --output-on-failure
```

Нужен репозиторий с поисковым API v1.2.0 и версия Go, совместимая
с его `go.mod`. Фиксируйте коммит и локальные изменения эталона.
CTest сообщает, если проверки Go не удалось включить; успешные автономные
тесты не означают успешную межъязыковую проверку. При проверке изменений
эталона отдельно запускайте в нём `go test ./...` и
`go test -tags purego ./...`.

Матрица совместимости проверяет сжатие и распаковку блоков и потоков
в обе стороны на каждом уровне, строгую обработку сжатых поисковых
таблиц, префиксы, sidecar, извлечение, объединённые потоки, пограничные
совпадения, случайный поиск, статистику и утилиту. В Windows дополнительно
проверяется путь к программе Go с пробелами.

Модульные тесты охватывают короткие выходные буферы, перекрывающиеся
копирования, длинные совпадения, CRC/EOF и индексированное чтение.
Создаваемые тестовые данные находятся в каталоге сборки; чистый репозиторий
не требует локальных сжатых файлов.

## Санитайзеры переносимой реализации (Linux, GCC/Clang)

```sh
cmake -S . -B build-asan -DCMAKE_BUILD_TYPE=Debug \
  -DMINLZ_ENABLE_ASM=OFF \
  -DMINLZ_ENABLE_INTRINSICS=OFF \
  -DMINLZ_ENABLE_INLINE_ASM=OFF \
  -DCMAKE_C_FLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer' \
  -DCMAKE_EXE_LINKER_FLAGS='-fsanitize=address,undefined'
cmake --build build-asan --parallel
ctest --test-dir build-asan --output-on-failure
```

## Необязательные реализации

Проверяйте каждый параметр отдельно, затем их сочетания:

- `MINLZ_ENABLE_EXPERIMENTAL_L2_SEARCH`: воспроизводимые входы с литералами
  и совпадениями около границ 512 КиБ и 2 МиБ, побайтовое сравнение
  с независимой ассемблерной реализацией и распаковка переносимым C и Go.
- `MINLZ_ENABLE_HUGEPAGE_WORKSPACE`: переход к большой выровненной области,
  отказ, повторное использование, сброс, рост, переполнение и обычное
  выделение памяти. Отдельный тест Linux перехватывает `madvise`,
  чтобы воспроизводимо проверить отклонение подсказки.
- `MINLZ_ENABLE_READSEEKER_BUFFER_TRANSFER`: вытеснение из смешанного кеша
  raw/compressed, ошибка CRC и повторный запрос, разреженные индексы,
  чтение через границы, общие reader, сброс и ограничение удерживаемой памяти.
  Повторите тесты с OFF и ON, включая санитайзеры.

## Установка

```sh
cmake --install build-release --config Release --prefix "$PWD/install-test"
cmake -S tests/package_consumer -B build-consumer \
  -DCMAKE_PREFIX_PATH="$PWD/install-test"
cmake --build build-consumer --config Release
```

Запустите полученную программу `minlz_package_consumer`. Она проверяет
установленный заголовок, экспорт CMake, зависимости компоновки и сжатие
с последующей распаковкой. Проверьте наличие обоих Readme, руководств
на двух языках, спецификаций и уведомлений о стороннем коде.
В PowerShell составляйте пути с помощью `$PWD.Path`.

## Проверка производительности

См. [измерения](PERFORMANCE.ru.md): семь выборок, сравнение размера
и скорости, уточняющие повторения. Модульные тесты, совместимость
и санитайзеры проверяют корректность; заявления о скорости требуют
отдельных измерений.
