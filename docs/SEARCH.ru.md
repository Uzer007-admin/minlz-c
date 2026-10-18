# Поиск и sidecar в MinLZ v1.2

Русский · [English](SEARCH.md) · [На главную](../Readme.ru.md)

Необязательная таблица перед блоком похожа на компактный фильтр Блума:
она может доказать отсутствие байтового шаблона и позволить пропустить
распаковку. Кандидаты проверяются по распакованным байтам, поэтому
коллизии увеличивают работу, но не дают ложных совпадений.

## Чанки формата

| Тип | Назначение |
| --- | --- |
| `0x44` | Конфигурация поиска |
| `0x45` | Несжатая битовая таблица с маскированным CRC32C |
| `0x46` | Разбитая на части сжатая битовая таблица |
| `0x47` | Ссылка sidecar на блок данных другого потока |

Обычный декодер пропускает эти чанки. Точное описание формата находится
в [SPEC_SEARCH.md](../SPEC_SEARCH.md).

## Конфигурация

Конфигурация имеет фиксированный размер и не выделяет память:

```c
minlz_search_config config;

minlz_search_config_init(&config);
minlz_search_config_set_match_len(&config, 6);
minlz_search_config_set_population(&config, 70, 25);
```

Фильтры префиксов уменьшают индекс структурированных данных:

```c
static const uint8_t prefixes[] = {'"', ':'};

minlz_search_config_set_match_len(&config, 4);
minlz_search_config_set_byte_prefix(&config, prefixes, sizeof(prefixes));
```

Длинный префикс содержит до 256 байт, дополнительные перекрывающиеся
окна — до 15 при условии `match_len + extras <= 16`.
`minlz_search_config_set_long_prefix` задаёт оба значения;
`minlz_search_config_set_extras` изменяет дополнительные окна.

Можно искать один длинный префикс, даже если запрос короче `match_len`.
Нулевая таблица позволяет пропускать обычные блоки. Для последнего блока,
у которого нет следующего перекрытия, поиск сохраняет отложенного кандидата
и проверяет его при EOF. Это правило действует для встроенных и sidecar-таблиц.

## Запись встроенных таблиц

```c
minlz_stream_options options;
minlz_writer *writer = NULL;

minlz_stream_options_init(&options);
options.search = &config;
minlz_writer_create(&writer, output_cb, output_ctx, &options);
minlz_writer_write(writer, input, input_len, NULL);
minlz_writer_close(writer);
minlz_writer_destroy(writer);
```

Примеры показывают порядок вызовов; в прикладном коде проверяйте каждый
возвращённый статус. `minlz_writer_set_search_table` и
`minlz_writer_set_sidecar` настраивают существующий writer.
При отдельном sidecar поисковые чанки и ссылки `0x47` записываются
в него, а основной поток содержит данные.

## Поиск

```c
minlz_block_searcher *searcher = NULL;

minlz_block_searcher_create(&searcher, input_cb, input_ctx);
minlz_block_searcher_set_max_block_size(searcher, MINLZ_MAX_BLOCK_SIZE);
minlz_block_searcher_search(searcher, pattern, pattern_len, on_match, match_ctx);
minlz_block_searcher_destroy(searcher);
```

Функция обратного вызова получает предыдущий и текущий блоки,
логическое смещение совпадения и абсолютное смещение распакованных данных.
Возвращаемые значения:

- `MINLZ_OK` — продолжить.
- `MINLZ_SEARCH_STOP` — успешно завершить.
- `MINLZ_SEARCH_FORWARD` — запросить следующий контекст.
- Другой статус — прервать с ошибкой.

Ненужный предыдущий блок заранее не распаковывается. Для его получения
внутри функции обратного вызова используйте:

```c
const uint8_t *previous = NULL;
size_t previous_len = 0;

minlz_search_result_load_previous(result, &previous, &previous_len);
```

Загруженный блок кешируется для повторных обращений. Результат и все
указатели на блоки действуют только до возврата из функции обратного вызова.

Поиск проверяет структуру потока, точные размеры, CRC32C, конечный размер
и пределы блоков. CRC можно явно отключить. Строгий режим bail возвращает
`MINLZ_SEARCH_TABLES_UNUSABLE` вместо распаковки блоков без пригодной таблицы.

## Отдельные индексы sidecar

Построение новых таблиц для существующего потока:

```c
minlz_search_config configs[2];

minlz_search_config_init(&configs[0]);
configs[1] = configs[0];
minlz_search_config_set_match_len(&configs[1], 4);
minlz_build_sidecar(sidecar_write_cb, sidecar_ctx,
                   stream_read_cb, stream_ctx, configs, 2, 0);
```

Извлечение встроенных таблиц с необязательной записью очищенного потока:

```c
minlz_extract_sidecar(sidecar_write_cb, sidecar_ctx,
                     stripped_write_cb, stripped_ctx,
                     stream_read_cb, stream_ctx);
```

Для sidecar-поиска передайте последовательное чтение индекса и `ReadAt`
основного потока:

```c
minlz_sidecar_searcher *searcher = NULL;

minlz_sidecar_searcher_create(&searcher,
    main_read_at_cb, main_ctx, sidecar_read_cb, sidecar_ctx);
minlz_sidecar_searcher_search(searcher,
    pattern, pattern_len, on_match, match_ctx);
minlz_sidecar_searcher_destroy(searcher);
```

Проверяемый кеш предварительного чтения объединяет заголовки, данные
и близкие блоки в меньшее число вызовов `ReadAt`, сохраняя проверки
границ и неполного чтения.

Оба варианта поиска сохраняют последние `pattern_len - 1` байт непрерывной
последовательности распакованных данных. Поэтому совпадение через три
и более блока не теряется, когда внутренний `Flush()` выдаёт короткий блок.

Поиск, построение и извлечение поддерживают объединённые потоки.
Ссылки на основной файл остаются абсолютными; смещения распакованных
данных начинаются заново на границе потока, как в Go v1.2.0.

## Сжатые таблицы и статистика

Реализован весь формат `0x46`: raw, RLE, разреженные подблоки,
индивидуальные и общие таблицы Huff0/FSE. Если сжатие невыгодно,
writer выбирает `0x45`, кроме явно принудительного режима.
Включённая часть zstd 1.5.7 не требует внешней библиотеки во время выполнения.

`minlz_search_config_set_compression_skip` ограничивает значение диапазоном
0–50 процентов и хранит его с точностью 0,01 процента.
`minlz_search_config_set_compression_stats_callback` предоставляет
статистику вариантов кодирования.

Подробная статистика поиска включается явно из-за дополнительной работы:

```c
minlz_block_searcher_set_collect_stats(searcher, 1);
```

После поиска используйте `minlz_block_searcher_stats`
и функции `minlz_block_searcher_window_*`. Для sidecar есть аналогичные
функции. `minlz_search_stats_fprint` и `minlz_search_stats_fprint_extended`
соответствуют Go `Fprint` и `FprintExtended`.

Совместимость определяется спецификациями и Go v1.2.0; CTest проверяет
префиксы, все уровни, Huff0, sidecar, объединённые потоки, конечный хвост,
следующий контекст, отложенный предыдущий блок и случайную матрицу
Go → C и C → Go. См. [совместимость](COMPATIBILITY.ru.md).
