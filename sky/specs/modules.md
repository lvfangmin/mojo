Sky Module System
=================

This document describes the Sky module system.

Overview
--------

The Sky module system is based on the ``import`` element. In its
most basic form, you import a module as follows:

```html
<import src="path/to/module.sky" />
```

As these ``import`` elements are inserted into a document, the
document's list of outstanding dependencies grows. When an imported
module completes, it is removed from the document's list of
outstanding dependencies.

Before executing script or inserting an element that is not already
registered, the parser waits until the list of outstanding
dependencies is empty. After the parser has finished parsing, the
document waits until its list of outstanding dependencies is empty
before the module it represents is marked complete.


Module API
----------

Within a script in a module, the ``module`` identifier is bound to
the ``Module`` object that represents the module.

### Exporting values ###

A module can export a value by assigning the ``exports`` property of
its ``Module`` object. By default, the ``exports`` property of a
``Module`` is an empty Object. Properties can be added to the object,
or, it can be set to an entirely different object; for example, it
could be set to the module's ``Document`` itself, in case the point of
the module is to expose some ``template`` elements.

### Exporting element definitions ###

When importing a module into another, Sky runs the following steps:
 - let export be the imported module's ``exports`` value
 - try to import export
 - if that fails:
    - try to import each property of export

"Try to import" a value means to run the following steps:
 - if the value is an element constructor (generated by
   ``registerElement()``), call this importer module's
   ``registerElement()`` with the value

### IDL ###

```javascript
dictionary InternalElementOptions {
  String tagName;
  Boolean shadow = false;
  Object prototype = Element;
}
interface InternalElementConstructorWithoutShadow {
  constructor (Module hostModule);
  attribute String tagName;
}
interface InternalElementConstructorWithShadow {
  constructor (Module hostModule);
  attribute String tagName;
  attribute Boolean shadow;
}
typedef ElementRegistrationOptions (InternalElementOptions or
                                    InternalElementConstructorWithoutShadow or
                                    InternalElementConstructorWithShadow);

abstract class AbstractModule : EventTarget {
  readonly attribute Document document; // O(1) // the Document of the module or application
  Promise<any> import(String url); // O(Yikes) // returns the module's exports
  private Array<Module> getImports(); O(N) // returns the Module objects of all the imported modules

  readonly attribute String url;

  ElementConstructor registerElement(Object options); // O(1)
  // if you call registerElement() with an object that was created by
  // registerElement(), it just returns the object after registering it,
  // rather than creating a new constructor
  // otherwise, it proceeds as follows:
  //  - if options is a Function (i.e. it is either an
  //    InternalElementConstructorWithoutShadow object or an
  //    InternalElementConstructorWithShadow object), then let
  //    constructor be that function, and let prototype be that
  //    functions's prototype; otherwise, let constructor be a no-op
  //    function and let prototype be the prototype property of the
  //    object passed in (the InternalElementOptions; prototype
  //    defaults to Element).
  //  - let shadow be option's shadow property's value coerced to a
  //    boolean, if the property is present, or else the value false.
  //  - let tagName be option's tagName property's value.
  //  - create a new Function that acts as if it had the signature of
  //    the constructors in the ElementConstructor interface, and that
  //    runs the follows steps when called:
  //      - throw if not called as a constructor
  //      - create an actual element object (the C++-backed object)
  //        called tagName, along with the specified attributes
  //      - initialise the shadow tree if shadow is true
  //      - call constructor, if it's not null, with the module
  //        within which the new element is being constructed as the
  //        argument
  //      - append all the specified children
  //  - mark that new Function as created by registerElement() so that
  //    it can be recognised if used as an argument to
  //    registerElement()
  //  - let that new Function's prototype be the aforementioned prototype
  //  - let that new Function have tagName and shadow properties set to
  //    the aforementioned tagName and shadow
  //  - register the new tagName with this constructor
  //  - return the new Function (which is, not coincidentally, an
  //    InternalElementConstructorWithShadow)

  readonly attribute ScriptElement? currentScript; // O(1) // returns the <script> element currently being executed if any, and if it's in this module; else null
}

class Module : AbstractModule {
  constructor (Application application, Document document, String url); // O(1)
  readonly attribute Application application; // O(1)

  attribute any exports; // O(1) // defaults to {}
}

class Application : AbstractModule {
  constructor (Document document, String url); // O(1)
  attribute String title; // O(1)
}
```

 
Naming modules
--------------

The ``as`` attribute on the ``import`` element binds a name to the
imported module:

```html
<import src="path/to/chocolate.sky" as="chocolate" />
```

The parser executes the contents of script elements inside a module as
if they were executed as follow:

```javascript
(new Function(name_1, ..., name_n, module, source_code)).call(
  value_1, ..., value_n, source_module);
```

Where ``name_1`` through ``name_n`` are the names bound to the
various named imports in the script element's document,
``source_code`` is the text content of the script element,
``source_module`` is the ``Module`` object of the script element's
module, and ``value_1`` through ``value_n`` are the values
exported by the various named imports in the script element's
document.

When an import fails to load, the ``as`` name for the import gets
bound to ``undefined``.
