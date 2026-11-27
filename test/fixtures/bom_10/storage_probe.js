(async () => {
  'use strict';
  const shape = fn => {
    let constructable = false;
    try { Reflect.construct(Object, [], fn); constructable = true; } catch (_) {}
    return {name: fn.name, length: fn.length,
      source: Function.prototype.toString.call(fn),
      ownPrototype: Object.hasOwn(fn, 'prototype'), constructable};
  };
  const describe = object => ({
    keys: Reflect.ownKeys(object).map(String),
    members: Object.fromEntries(Reflect.ownKeys(object).map(key => {
      const desc = Object.getOwnPropertyDescriptor(object, key);
      const result = {enumerable: desc.enumerable, configurable: desc.configurable};
      if ('value' in desc) {
        Object.assign(result, {kind: 'data', writable: desc.writable,
          value: typeof desc.value === 'function' ? shape(desc.value) :
            (desc.value === null || ['string', 'boolean', 'number'].includes(typeof desc.value)
              ? desc.value : typeof desc.value)});
      } else {
        Object.assign(result, {kind: 'accessor', get: desc.get ? shape(desc.get) : null,
          set: desc.set ? shape(desc.set) : null});
      }
      return [String(key), result];
    }))
  });
  const error = exc => ({name: exc.name, message: exc.message,
    code: typeof exc.code === 'number' ? exc.code : null,
    domException: exc instanceof DOMException});
  const invoke = async (fn, project = value => value === undefined ? 'undefined' : value) => {
    let result;
    try { result = fn(); } catch (exc) { return {outcome: 'threw', error: error(exc)}; }
    const promise = result instanceof Promise;
    try { return {outcome: 'fulfilled', promise, value: project(await result)}; }
    catch (exc) { return {outcome: 'rejected', promise, error: error(exc)}; }
  };
  const out = {secure: isSecureContext, storagePresent: 'storage' in navigator,
    interfaces: {}, behavior: {}};
  for (const name of ['StorageManager', 'FileSystemHandle', 'FileSystemDirectoryHandle',
      'FileSystemFileHandle', 'FileSystemWritableFileStream', 'FileSystemSyncAccessHandle']) {
    const ctor = globalThis[name];
    out.interfaces[name] = typeof ctor === 'undefined' ? {present: false} : {
      present: true, constructor: shape(ctor), statics: describe(ctor),
      prototype: describe(ctor.prototype),
      parent: Object.getPrototypeOf(ctor.prototype)?.constructor?.name || null};
  }
  const storageDesc = Object.getOwnPropertyDescriptor(Navigator.prototype, 'storage');
  out.storageDescriptor = storageDesc ? {
    enumerable: storageDesc.enumerable, configurable: storageDesc.configurable,
    get: storageDesc.get ? shape(storageDesc.get) : null,
    set: storageDesc.set ? shape(storageDesc.set) : null} : null;
  if (out.storagePresent) {
    out.storage = {tag: Object.prototype.toString.call(navigator.storage),
      ownKeys: Reflect.ownKeys(navigator.storage).map(String),
      sameObject: navigator.storage === navigator.storage,
      getDirectoryPresent: typeof navigator.storage.getDirectory === 'function'};
    if (out.storage.getDirectoryPresent) {
      const fn = StorageManager.prototype.getDirectory;
      out.behavior.badReceiver = await invoke(() => fn.call({}));
      out.behavior.root = await invoke(() => navigator.storage.getDirectory(), root => ({
        tag: Object.prototype.toString.call(root), kind: root.kind, name: root.name,
        ownKeys: Reflect.ownKeys(root).map(String),
        directoryInstance: root instanceof FileSystemDirectoryHandle,
        handleInstance: root instanceof FileSystemHandle}));
      if (out.behavior.root.outcome === 'fulfilled') {
        const root = await navigator.storage.getDirectory();
        const another = await navigator.storage.getDirectory();
        out.behavior.rootIdentity = {sameWrapper: root === another,
          sameEntry: await root.isSameEntry(another)};
        out.behavior.queryPermission = await invoke(() => root.queryPermission({mode: 'readwrite'}));
        out.behavior.noName = await invoke(() => root.getDirectoryHandle());
        out.behavior.emptyName = await invoke(() => root.getDirectoryHandle(''));
        out.behavior.parentName = await invoke(() => root.getDirectoryHandle('..'));
        out.behavior.pathName = await invoke(() => root.getFileHandle('a/b'));
        out.behavior.missing = await invoke(() => root.getFileHandle('missing.txt'));
        const directory = await root.getDirectoryHandle('bom10-reference', {create: true});
        try {
          const file = await directory.getFileHandle('note.txt', {create: true});
          out.behavior.createdFile = {kind: file.kind, name: file.name,
            fileInstance: file instanceof FileSystemFileHandle};
          out.behavior.emptyFile = await invoke(() => file.getFile(), value => ({
            name: value.name, size: value.size, type: value.type, fileInstance: value instanceof File}));
          out.behavior.entries = [];
          for await (const [name, handle] of directory.entries())
            out.behavior.entries.push([name, handle.kind]);
          out.behavior.entries.sort();
          out.behavior.resolveFile = await root.resolve(file);
          out.behavior.resolveSelf = await root.resolve(root);
          out.behavior.resolveOutside = await directory.resolve(root);
          out.behavior.wrongKind = await invoke(() => directory.getDirectoryHandle('note.txt'));
          out.behavior.badPermission = await invoke(() => file.queryPermission({mode: 'invalid'}));
          out.behavior.badHandleReceiver = await invoke(() => FileSystemHandle.prototype.isSameEntry.call({}, root));
          const stream = await file.createWritable();
          out.behavior.writable = {tag: Object.prototype.toString.call(stream),
            instance: stream instanceof FileSystemWritableFileStream,
            writableStreamInstance: stream instanceof WritableStream};
          await stream.write('hello');
          await stream.close();
          out.behavior.writtenText = await (await file.getFile()).text();
          out.behavior.removeNonempty = await invoke(() => root.removeEntry('bom10-reference'));
        } finally {
          await root.removeEntry('bom10-reference', {recursive: true});
        }
        out.behavior.afterRemove = await invoke(() => root.getDirectoryHandle('bom10-reference'));
      }
    }
  }
  globalThis.__bom10StorageResult = out;
  return out;
})()
