# Paper inputs

Each JSON manifest pins the complete input files by name, byte count and SHA-256.
Inputs are encoded independently; reported corpus times sum matching per-file
passes before taking the median. No column or file is duplicated.

| Dataset | Bytes | Acquisition / preparation |
|---|---:|---|
| [DBText](dbtext.json) | 39,841,347 | 23 original files from the [pinned FSST sources](DBTEXT_SOURCES.json). Run `python3 datasets/fetch_dbtext.py --out work/data/dbtext`. |
| [Python](python.json) | 99,999,986 | The exact supervisor-supplied corpus, included in `archives/python.zip`. The original upstream selection is not recorded. It is not a checkout of CPython. |
| [Yelp](yelp.json) | 100,000,488 | `yelp_academic_dataset_business.json` from the [Yelp dataset](https://business.yelp.com/external-assets/files/Yelp-JSON.zip). Keep complete lines through the first line ending at or beyond 100,000,000 bytes. The checked upstream member was 118,863,795 bytes. |
| [OpenStack](openstack.json) | 61,442,082 | The three complete `.log` files in [LogHub's OpenStack archive](https://zenodo.org/records/8196385/files/OpenStack.tar.gz?download=1), including the abnormal log. |
| [SQLStorm TPC-DS](sqlstorm-tpcds.json) | 18,444,591 | The 15,242 files under `v1.0/tpcds/queries` at [commit b3bb0b9](https://github.com/SQL-Storm/SQLStorm/tree/b3bb0b96794a6afe9bb8f3ff2b243562b779c40d/v1.0/tpcds/queries). Sort relative paths lexicographically; append one NUL byte after each unchanged SQL file. This is the TPC-DS subset. [Source receipt](sqlstorm-source.json). |

All five prepared datasets are bundled in `archives/` as ordinary Git ZIP files.
[ARCHIVES.json](ARCHIVES.json) pins each archive's bytes and SHA-256. Each ZIP
contains only the corresponding dataset directory and its complete pinned files.
The ZIP compression is a transport format; it is not a measured baseline result.
Dataset owners' original terms apply; the toolkit license does not relicense data.

From the repository root:

```sh
python3 datasets/fetch.py --out work/data
```

This requires only Python's standard library and no additional download. It checks
the ZIP hashes and every extracted file, rejects extra or duplicate members, and
requires a new or empty destination. ZIPs use fixed timestamps and preserve all
original file bytes, including SQLStorm NUL separators.

For an independent upstream acquisition check, use `fetch_upstream.py` with the
exact Python source and the official Yelp business JSON:

```sh
python3 datasets/fetch_upstream.py --python-source /path/to/python-source.py --yelp-source /path/to/yelp_academic_dataset_business.json --out work/upstream-data
```

Individual preparation commands remain available:

```sh
python3 datasets/prepare.py python --source /path/to/python-source.py --out work/data/python
python3 datasets/prepare.py yelp --source /path/to/yelp_academic_dataset_business.json --out work/data/yelp
python3 datasets/prepare.py openstack --source /path/to/OpenStack.tar.gz --out work/data/openstack
python3 datasets/prepare.py sqlstorm-tpcds --source /path/to/SQLStorm/v1.0/tpcds/queries --out work/data/sqlstorm-tpcds
python3 datasets/prepare.py dbtext --out work/data/dbtext --check
```

`--check` works for each dataset and never modifies it. Preparation requires a
new output directory. Source SQL bytes, row order, punctuation and terminators
are preserved; the SQLStorm NUL separators are included in the measured size.
