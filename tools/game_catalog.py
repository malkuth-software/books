#!/usr/bin/env python3
"""Validated folder discovery, shared by CMake, asset preparation and authoring tools."""
from __future__ import annotations
import argparse
import json
import re
from pathlib import Path

IDENTIFIER = re.compile(r'[a-z][a-z0-9_]*\Z')
CPP_KEYWORDS = frozenset("""
    alignas alignof and and_eq asm atomic_cancel atomic_commit atomic_noexcept auto
    bitand bitor bool break case catch char char8_t char16_t char32_t class compl
    concept const consteval constexpr constinit const_cast continue co_await co_return
    co_yield decltype default delete do double dynamic_cast else enum explicit export
    extern false float for friend goto if inline int long mutable namespace new noexcept
    not not_eq nullptr operator or or_eq private protected public reflexpr register
    reinterpret_cast requires return short signed sizeof static static_assert static_cast
    struct switch synchronized template this thread_local throw true try typedef typeid
    typename union unsigned using virtual void volatile wchar_t while xor xor_eq
""".split())
# The shelf's categories, in the order it shows them; src/suite.cpp names them.
CATEGORIES = ('cards', 'puzzles', 'matching', 'skill', 'quiet')
RESERVED_TOPICS = frozenset(('rules', 'help', 'about'))


def cpp_identifier(value):
    return (isinstance(value, str) and IDENTIFIER.fullmatch(value)
            and value not in CPP_KEYWORDS and '__' not in value)


def discover_engines(root: Path) -> dict[str, dict]:
    """Shared engines: shared/<id>/ENGINE.json. Collection infrastructure that several
    games build on, discovered like games and validated before any game uses one."""
    root = Path(root).resolve()
    engines = {}
    namespaces = {}
    for path in sorted((root / 'shared').glob('*/ENGINE.json')):
        def fail(message):
            raise ValueError(f'{path}: {message}')
        try:
            data = json.loads(path.read_text(encoding="utf-8"))
        except (OSError, json.JSONDecodeError) as error:
            fail(str(error))
        if not isinstance(data, dict):
            fail('manifest must be a JSON object')
        if type(data.get('schema_version')) is not int or data['schema_version'] != 1:
            fail('schema_version must be 1')
        for key in ('id', 'namespace'):
            if not cpp_identifier(data.get(key)):
                fail(f'{key} must be a nonreserved lowercase C++ identifier')
        if data['id'] != path.parent.name:
            fail('id must equal the folder name')
        if data['namespace'] in ('std', 'games', 'gui_forms', 'kit', 'gf'):
            fail('namespace is reserved by the application or toolkit')
        if data['namespace'] in namespaces:
            fail(f'duplicate namespace {data["namespace"]!r}, already used by {namespaces[data["namespace"]]}')
        namespaces[data['namespace']] = path
        for key in ('title', 'summary'):
            if not isinstance(data.get(key), str) or not data[key].strip():
                fail(f'{key} must be nonempty text')
        def local(value, directory=False):
            if (not isinstance(value, str) or not value or Path(value).is_absolute()
                    or any(c in value for c in (';', '"', '$', '\\', '\n', '\r', '\0'))):
                fail(f'invalid file path {value!r}')
            candidate = (path.parent / value).resolve()
            if not candidate.is_relative_to(path.parent):
                fail(f'engine path escapes its folder: {value}')
            if not (candidate.is_dir() if directory else candidate.is_file()):
                fail(f'missing {value}')
        local(data.get('build'))
        for key in ('ui_sources', 'tests'):
            data.setdefault(key, [])
            if not isinstance(data[key], list):
                fail(f'{key} must be a list')
            for value in data[key]:
                local(value)
        for key in ('libraries', 'ui_libraries'):
            data.setdefault(key, [])
            if not isinstance(data[key], list) or any(not isinstance(v, str) or not IDENTIFIER.fullmatch(v) for v in data[key]):
                fail(f'{key} must contain CMake target identifiers')
        for key in ('source_directories', 'ui_directories'):
            data.setdefault(key, [])
            if not isinstance(data[key], list):
                fail(f'{key} must be a list')
            for value in data[key]:
                local(value, True)
        # Engines an engine builds on (shared/<id>/ENGINE.json), by id.
        data.setdefault('engines', [])
        if (not isinstance(data['engines'], list) or any(not isinstance(e, str) for e in data['engines'])
                or len(set(data['engines'])) != len(data['engines']) or data['id'] in data['engines']):
            fail('engines must be a list of distinct other engine ids')
        data['directory'] = path.parent
        data['manifest_path'] = path
        engines[data['id']] = data
    for engine in engines.values():
        for needed in engine['engines']:
            if needed not in engines:
                raise ValueError(f'{engine["manifest_path"]}: unknown engine {needed!r}; engines live in shared/<id>/ENGINE.json')
    for engine_id in engines:
        if engine_id in engine_closure(engines, [engine_id])[1:]:
            raise ValueError(f'{engines[engine_id]["manifest_path"]}: engines depend on each other in a circle')
    return engines


def engine_closure(engines: dict[str, dict], ids) -> list[str]:
    """The engines named and, after each, the engines it builds on, transitively; each once."""
    result = []
    def visit(engine_id, trail):
        if engine_id in result:
            return
        result.append(engine_id)
        if engine_id in trail:
            return
        for needed in engines[engine_id].get('engines', []):
            visit(needed, trail + [engine_id])
    for engine_id in ids:
        visit(engine_id, [])
    return result


def discover(root: Path, extra_dirs=(), include_disabled=False) -> list[dict]:
    root = Path(root).resolve()
    engines = discover_engines(root)
    engine_namespaces = {e['namespace']: e['manifest_path'] for e in engines.values()}
    paths = set((root / 'books').glob('*/GAME.json'))
    paths.update(Path(d).resolve() / 'GAME.json' for d in extra_dirs)
    result = []
    used = {key: {} for key in ('id', 'entry_id', 'entry_name', 'namespace')}
    for path in sorted({p.resolve() for p in paths}):
        def fail(message):
            raise ValueError(f'{path}: {message}')
        try:
            data = json.loads(path.read_text(encoding="utf-8"))
        except (OSError, json.JSONDecodeError) as error:
            fail(str(error))
        if not isinstance(data, dict):
            fail('manifest must be a JSON object')
        if type(data.get('schema_version')) is not int or data['schema_version'] != 1:
            fail('schema_version must be 1')
        data.setdefault('entry_name', data.get('id'))
        for key in ('id', 'entry_name', 'namespace'):
            if not cpp_identifier(data.get(key)):
                fail(f'{key} must be a nonreserved lowercase C++ identifier')
        if data['namespace'] in ('std', 'games', 'gui_forms'):
            fail('namespace is reserved by the application or toolkit')
        if data['namespace'] in engine_namespaces:
            fail(f'namespace {data["namespace"]!r} belongs to the engine at {engine_namespaces[data["namespace"]]}')
        data.setdefault('engines', [])
        if (not isinstance(data['engines'], list) or any(not isinstance(e, str) for e in data['engines'])
                or len(set(data['engines'])) != len(data['engines'])):
            fail('engines must be a list of distinct engine ids')
        for engine in data['engines']:
            if engine not in engines:
                fail(f'unknown engine {engine!r}; engines live in shared/<id>/ENGINE.json')
        if type(data.get('entry_id')) is not int or not 0 <= data['entry_id'] <= 2147483647:
            fail('entry_id must be a permanent integer from 0 to 2147483647')
        for key in used:
            if data[key] in used[key]:
                fail(f'duplicate {key} {data[key]!r}, already reserved in {used[key][data[key]]}')
            used[key][data[key]] = path
        for key in ('enabled', 'hosted', 'rail'):
            if key in data and type(data[key]) is not bool:
                fail(f'{key} must be boolean')
        data['directory'] = path.parent
        data['manifest_path'] = path
        if not data.get('enabled', True):
            if include_disabled:
                result.append(data)
            continue
        for key in ('title', 'kind', 'blurb'):
            if not isinstance(data.get(key), str) or not data[key].strip():
                fail(f'{key} must be nonempty text')
        if data.get('category') not in CATEGORIES:
            fail('category must be one of ' + ', '.join(CATEGORIES))
        def local_file(value, directory=False):
            if (not isinstance(value, str) or not value or Path(value).is_absolute()
                    or any(c in value for c in (';', '"', '$', '\\', '\n', '\r', '\0'))):
                fail(f'invalid file path {value!r}')
            candidate = (path.parent / value).resolve()
            if not candidate.is_relative_to(path.parent):
                fail(f'module path escapes its folder: {value}')
            if not (candidate.is_dir() if directory else candidate.is_file()):
                fail(f'missing {value}')
        for key in ('module', 'cover', 'help', 'build'):
            local_file(data.get(key))
        for key in ('ui_sources', 'audio_sources'):
            data.setdefault(key, [])
            if not isinstance(data[key], list):
                fail(f'{key} must be a list')
            for value in data[key]:
                local_file(value)
        data.setdefault('libraries', [])
        if not isinstance(data['libraries'], list) or any(not isinstance(v,str) or not IDENTIFIER.fullmatch(v) for v in data['libraries']):
            fail('libraries must contain CMake target identifiers')
        colors = data.get('colors')
        if not isinstance(colors, list) or len(colors) != 3 or any(not isinstance(c, list) or len(c) != 3 or any(type(v) is not int or not 0 <= v <= 255 for v in c) for c in colors):
            fail('colors must be three RGB triples')
        topics = data.setdefault('help_topics', [])
        if not isinstance(topics, list):
            fail('help_topics must be a list')
        topic_ids = set()
        for topic in topics:
            if (not isinstance(topic, dict) or not isinstance(topic.get('id'), str)
                    or not IDENTIFIER.fullmatch(topic['id'])
                    or not isinstance(topic.get('title'), str) or not topic['title'].strip()):
                fail('each help topic needs id, title and file')
            if topic['id'] in RESERVED_TOPICS:
                fail(f'reserved help topic {topic["id"]}')
            if topic['id'] in topic_ids:
                fail(f'duplicate help topic {topic["id"]}')
            topic_ids.add(topic['id'])
            local_file(topic.get('file'))
        if 'assets' not in data and (path.parent / 'assets').is_dir():
            data['assets'] = 'assets'
        if data.get('assets'):
            local_file(data['assets'], True)
        result.append(data)
    result.sort(key=lambda game: game['entry_id'])
    return result

def quoted(value):
    return json.dumps(value, ensure_ascii=False)

def generate(root: Path, output: Path, extra_dirs=()):
    games = discover(root, extra_dirs)
    if not games:
        raise ValueError('the collection requires at least one enabled game')
    output.mkdir(parents=True, exist_ok=True)
    enum = ',\n'.join(f'    {g["entry_name"]} = {g["entry_id"]}' for g in games)
    entries = ', '.join(f'Entry::{g["entry_name"]}' for g in games)
    header = f'''#pragma once
#include <array>
namespace games {{
enum class Entry : int {{
{enum}
}};
inline constexpr int entry_count = {len(games)};
inline constexpr std::array<Entry, entry_count> entries{{{{{entries}}}}};
inline constexpr int entry_index(Entry entry) {{
    for (int i=0;i<entry_count;++i) if (entries[i]==entry) return i;
    return -1;
}}
inline constexpr bool valid_entry(Entry entry) {{ return entry_index(entry)>=0; }}
}}
'''
    cpp = '#include "game_module.hpp"\n#include <stdexcept>\nnamespace games {\nnamespace modules {\n'
    for g in games:
        cpp += f'std::unique_ptr<GameInstance> {g["id"]}_create(ModuleContext&);\nvoid {g["id"]}_cover(gf::Painter&,gf::Rect);\n'
    cpp += '}\nnamespace {\nconst std::array<GameDescriptor,entry_count> catalog{{\n'
    for g in games:
        colors = ','.join('gf::Color::rgba('+','.join(map(str,c))+')' for c in g['colors'])
        info = ','.join(quoted(g[k]) for k in ('title','kind','blurb'))+','+colors+','+str(CATEGORIES.index(g['category']))
        topics = ','.join('{'+','.join(quoted(v) for v in (t['id'], t['title'], (g['directory']/t['file']).read_text(encoding="utf-8").strip()))+'}' for t in g['help_topics'])
        cpp += '{Entry::'+g['entry_name']+','+quoted(g['id'])+',{'+info+'},'+quoted((g['directory']/g['help']).read_text(encoding="utf-8").strip())+',{'+topics+'},'+str(g.get('hosted',False)).lower()+','+str(g.get('rail',False)).lower()+',modules::'+g['id']+'_create,modules::'+g['id']+'_cover},\n'
    cpp += '''}};
}
const GameDescriptor& game_descriptor(Entry entry) {
    const int index=entry_index(entry);
    if(index<0) throw std::out_of_range("Unknown game entry");
    return catalog[index];
}
std::optional<Entry> find_entry(std::string_view id) {
    for (const GameDescriptor& game:catalog) if(id==game.id) return game.entry;
    return std::nullopt;
}
}
'''
    engines = discover_engines(root)
    # An engine used brings the engines it builds on.
    used_engines = sorted(set(engine_closure(engines, sorted({e for g in games for e in g['engines']}))))
    metadata = '# Generated discovery metadata; safe to include without creating build targets.\n'
    metadata += 'set(GAMES_MODULE_IDS ' + ' '.join(g['id'] for g in games) + ')\n'
    metadata += 'set(GAMES_ENGINE_IDS ' + ' '.join(sorted(engines)) + ')\n'
    cmake = '# Generated from module folders; do not edit.\n'
    cmake += 'include("${CMAKE_CURRENT_LIST_DIR}/game_catalog_metadata.cmake")\n'
    # Engines first: every engine builds and tests its core; games link to it.
    for engine_id in sorted(engines):
        e = engines[engine_id]
        d = e['directory'].as_posix()
        if any(c in d for c in (';', '"', '$', '\n')):
            raise ValueError(f'unsupported CMake path: {d}')
        metadata += f'set(GAMES_ENGINE_DIRECTORY_{engine_id} "{d}")\n'
        cmake += f'set(ENGINE_DIR "{d}")\ninclude("{d}/{e["build"]}")\n'
    # An engine's interface code is compiled once, and only when a game uses it.
    for engine_id in used_engines:
        e = engines[engine_id]
        d = e['directory'].as_posix()
        for value in e['ui_sources']:
            cmake += f'list(APPEND GAMES_MODULE_UI "{d}/{value}")\n'
        for value in e['ui_libraries']:
            cmake += f'list(APPEND GAMES_MODULE_LIBRARIES {value})\n'
    for g in games:
        # This path is also the working directory for each module's included CMake.
        d = g['directory'].as_posix()
        if any(c in d for c in (';', '"', '$', '\n')):
            raise ValueError(f'unsupported CMake path: {d}')
        metadata += f'set(GAMES_MODULE_DIRECTORY_{g["id"]} "{d}")\n'
        for key, values in (('UI', g['ui_sources']), ('AUDIO', g['audio_sources'])):
            paths = ' '.join(f'"{d}/{value}"' for value in values)
            metadata += f'set(GAMES_MODULE_{key}_{g["id"]} {paths})\n'
        metadata += f'set(GAMES_MODULE_LIBRARIES_{g["id"]} ' + ' '.join(g['libraries']) + ')\n'
        metadata += f'set(GAMES_MODULE_ENGINES_{g["id"]} ' + ' '.join(g['engines']) + ')\n'
        cmake += f'set(GAME_MODULE_DIR "{d}")\ninclude("{d}/{g["build"]}")\n'
        for variable,values in (('GAMES_MODULE_SOURCES',[g['module'],g['cover']]),('GAMES_MODULE_UI',g['ui_sources']),('GAMES_MODULE_AUDIO',g['audio_sources'])):
            for value in values:
                cmake += f'list(APPEND {variable} "{d}/{value}")\n'
        for value in g['libraries']:
            cmake += f'list(APPEND GAMES_MODULE_LIBRARIES {value})\n'
        for value in [g['help']]+[t['file'] for t in g['help_topics']]:
            cmake += f'set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "{d}/{value}")\n'
    for name, content in (('game_entries.hpp',header),('game_registry.cpp',cpp),('game_modules.cmake',cmake),('game_catalog_metadata.cmake',metadata)):
        p=output/name
        if not p.exists() or p.read_text(encoding="utf-8")!=content:
            p.write_text(content, encoding="utf-8")
    return games

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root',type=Path,default=Path(__file__).resolve().parents[1])
    parser.add_argument('--extra',action='append',default=[],type=Path)
    parser.add_argument('--generate',type=Path)
    args=parser.parse_args()
    try:
        games=generate(args.root,args.generate,args.extra) if args.generate else discover(args.root,args.extra,True)
    except ValueError as error:
        parser.exit(1,str(error)+'\n')
    for g in games:
        on = (' on ' + ', '.join(g['engines'])) if g.get('engines') else ''
        print(f'{g["entry_id"]:6}  {g["id"]}'+(' (reserved)' if not g.get('enabled',True) else '')+on)
if __name__=='__main__':
    main()
