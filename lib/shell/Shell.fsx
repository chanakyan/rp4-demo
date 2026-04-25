// SPDX-License-Identifier: BSD-2-Clause
// Shell.fsx — F# DSL for grep/find/sed/awk/zsh-style operations
//
// Usage: #load "lib/Shell.fsx"
//        open Shell

open System
open System.IO
open System.Text.RegularExpressions
open System.Diagnostics

[<AutoOpen>]
module Shell =

    // ─── find ───────────────────────────────────────────────────────────
    // find "~/rp4" "*.pdf" ["/vendor/"; "/.git/"]

    let find (dir: string) (glob: string) (skip: string list) =
        let d = dir.Replace("~", Environment.GetFolderPath(Environment.SpecialFolder.UserProfile))
        Directory.EnumerateFiles(d, glob, SearchOption.AllDirectories)
        |> Seq.filter (fun p -> skip |> List.forall (fun s -> not (p.Contains(s))))
        |> Seq.toArray

    let findDirs (dir: string) (skip: string list) =
        let d = dir.Replace("~", Environment.GetFolderPath(Environment.SpecialFolder.UserProfile))
        Directory.EnumerateDirectories(d, "*", SearchOption.AllDirectories)
        |> Seq.filter (fun p -> skip |> List.forall (fun s -> not (p.Contains(s))))
        |> Seq.toArray

    let ls (dir: string) =
        let d = dir.Replace("~", Environment.GetFolderPath(Environment.SpecialFolder.UserProfile))
        Directory.EnumerateFileSystemEntries(d) |> Seq.map Path.GetFileName |> Seq.sort |> Seq.toArray

    // ─── read ───────────────────────────────────────────────────────────

    let lines (path: string) = File.ReadAllLines(path)
    let text (path: string) = File.ReadAllText(path)
    let head n (arr: 'a array) = arr.[.. min (n-1) (arr.Length-1)]
    let tail n (arr: 'a array) = arr.[max 0 (arr.Length - n) ..]
    let skip n (arr: 'a array) = arr.[min n arr.Length ..]

    // ─── grep ───────────────────────────────────────────────────────────
    // grep @"SPDX" (lines "file.tex")
    // grepi for case-insensitive
    // grepv for invert match

    let grep (pattern: string) (lines: string array) =
        lines |> Array.filter (fun l -> Regex.IsMatch(l, pattern))

    let grepi (pattern: string) (lines: string array) =
        lines |> Array.filter (fun l -> Regex.IsMatch(l, pattern, RegexOptions.IgnoreCase))

    let grepv (pattern: string) (lines: string array) =
        lines |> Array.filter (fun l -> not (Regex.IsMatch(l, pattern)))

    let grepn (pattern: string) (lines: string array) =
        lines |> Array.indexed |> Array.filter (fun (_, l) -> Regex.IsMatch(l, pattern))

    let grepCount (pattern: string) (lines: string array) =
        lines |> Array.filter (fun l -> Regex.IsMatch(l, pattern)) |> Array.length

    // grep across files — like grep -rl
    let rgrep (pattern: string) (files: string array) =
        files |> Array.filter (fun f ->
            try File.ReadAllLines(f) |> Array.exists (fun l -> Regex.IsMatch(l, pattern))
            with _ -> false)

    // ─── sed ────────────────────────────────────────────────────────────
    // sed @"\d+" "NUM" "there are 42 cats"
    // replace [("old","new"); ("a","b")] "string"

    let sed (pattern: string) (repl: string) (s: string) =
        Regex.Replace(s, pattern, repl)

    let replace (pairs: (string * string) list) (s: string) =
        pairs |> List.fold (fun (acc: string) (o, n) -> acc.Replace(o, n)) s

    let sedLines (pattern: string) (repl: string) (lines: string array) =
        lines |> Array.map (fun l -> Regex.Replace(l, pattern, repl))

    let tr (from: char) (dest: char) (s: string) = s.Replace(from, dest)

    // ─── awk ────────────────────────────────────────────────────────────
    // cut '\t' 2 "a\tb\tc" -> "b"
    // fields '\t' "a\tb\tc" -> [|"a";"b";"c"|]

    let fields (sep: char) (line: string) =
        line.Split(sep, StringSplitOptions.None)

    let cut (sep: char) (idx: int) (line: string) =
        let f = line.Split(sep, StringSplitOptions.None)
        if idx < f.Length then f.[idx] else ""

    let nf (sep: char) (line: string) =
        line.Split(sep, StringSplitOptions.None).Length

    let join (sep: string) (parts: string seq) = String.Join(sep, parts)

    // CSV with quoted fields
    let csvFields (line: string) =
        let result = System.Collections.Generic.List<string>()
        let buf = System.Text.StringBuilder()
        let mutable inQuote = false
        for c in line do
            match c with
            | '"' -> inQuote <- not inQuote
            | ',' when not inQuote ->
                result.Add(buf.ToString())
                buf.Clear() |> ignore
            | _ -> buf.Append(c) |> ignore
        result.Add(buf.ToString())
        result.ToArray()

    // ─── regex capture ──────────────────────────────────────────────────
    // capture @"version (\d+)\.(\d+)" "version 3.14" -> Some [|"3";"14"|]

    let capture (pattern: string) (s: string) =
        let m = Regex.Match(s, pattern)
        if m.Success then
            Some (m.Groups |> Seq.cast<Group> |> Seq.skip 1 |> Seq.map (fun g -> g.Value) |> Seq.toArray)
        else None

    let captureAll (pattern: string) (s: string) =
        Regex.Matches(s, pattern)
        |> Seq.cast<Match>
        |> Seq.map (fun m -> m.Groups |> Seq.cast<Group> |> Seq.skip 1 |> Seq.map (fun g -> g.Value) |> Seq.toArray)
        |> Seq.toArray

    // ─── write ──────────────────────────────────────────────────────────

    let write (path: string) (content: string) = File.WriteAllText(path, content)
    let writeLines (path: string) (lines: string array) = File.WriteAllLines(path, lines)
    let append (path: string) (content: string) = File.AppendAllText(path, content)

    // ─── zsh essentials ─────────────────────────────────────────────────

    // Run a shell command, return stdout
    let sh (cmd: string) =
        let psi = ProcessStartInfo("/bin/zsh", sprintf "-c \"%s\"" (cmd.Replace("\"", "\\\"")))
        psi.RedirectStandardOutput <- true
        psi.RedirectStandardError <- true
        psi.UseShellExecute <- false
        let p = Process.Start(psi)
        let out = p.StandardOutput.ReadToEnd()
        p.WaitForExit()
        out.TrimEnd()

    // Run, return exit code + stdout + stderr
    let shFull (cmd: string) =
        let psi = ProcessStartInfo("/bin/zsh", sprintf "-c \"%s\"" (cmd.Replace("\"", "\\\"")))
        psi.RedirectStandardOutput <- true
        psi.RedirectStandardError <- true
        psi.UseShellExecute <- false
        let p = Process.Start(psi)
        let out = p.StandardOutput.ReadToEnd()
        let err = p.StandardError.ReadToEnd()
        p.WaitForExit()
        (p.ExitCode, out.TrimEnd(), err.TrimEnd())

    // wc -l equivalent
    let wc (path: string) = File.ReadAllLines(path).Length

    // stat — file size in bytes
    let fileSize (path: string) = FileInfo(path).Length

    // md5 / sha256
    let md5 (path: string) = sh (sprintf "md5 -q '%s'" path)
    let sha256 (path: string) = sh (sprintf "shasum -a 256 '%s'" path) |> fun s -> s.Split(' ').[0]

    // which
    let which (name: string) =
        let r = sh (sprintf "which %s" name)
        if r = "" then None else Some r

    // xargs-style: apply f to each element, collect results
    let xargs (f: string -> string) (items: string array) =
        items |> Array.map f

    // sort | uniq
    let uniq (lines: string array) = lines |> Array.distinct
    let sortUniq (lines: string array) = lines |> Array.distinct |> Array.sort
    let freq (lines: string array) =
        lines |> Array.countBy id |> Array.sortByDescending snd

    // tee — print and pass through
    let tee (lines: string array) =
        lines |> Array.iter (printfn "%s")
        lines

    // diff two string arrays — lines in a not in b
    let diff (a: string array) (b: string array) =
        let bSet = Set.ofArray b
        a |> Array.filter (fun l -> not (bSet.Contains l))

    // ─── pipe helper ────────────────────────────────────────────────────
    // Just use |> — F# pipes ARE the DSL

    // ─── path helpers ───────────────────────────────────────────────────

    let home = Environment.GetFolderPath(Environment.SpecialFolder.UserProfile)
    let expand (path: string) = path.Replace("~", home)
    let basename (path: string) = Path.GetFileName(path)
    let dirname (path: string) = Path.GetDirectoryName(path)
    let ext (path: string) = Path.GetExtension(path)
    let stem (path: string) = Path.GetFileNameWithoutExtension(path)
    let exists (path: string) = File.Exists(expand path) || Directory.Exists(expand path)

    // ─── tex helpers ────────────────────────────────────────────────────

    let texEscape (s: string) =
        replace [("\\", "\\textbackslash{}"); ("%", "\\%"); ("&", "\\&");
                 ("#", "\\#"); ("_", "\\_"); ("{", "\\{"); ("}", "\\}");
                 ("~", "\\textasciitilde{}"); ("^", "\\textasciicircum{}")] s

    let texRaw (lines: string list) =
        lines |> List.map (fun l -> l + "\n") |> String.concat ""

    // ─── http ───────────────────────────────────────────────────────────
    // get "https://api.example.com/data"
    // post "https://api.example.com/data" """{"key":"value"}"""
    // getJson "url" -> parsed via simple extraction

    open System.Net.Http

    let private client = new HttpClient()

    let get (url: string) =
        client.GetStringAsync(url).Result

    let getBytes (url: string) =
        client.GetByteArrayAsync(url).Result

    let post (url: string) (body: string) =
        let content = new StringContent(body, Text.Encoding.UTF8, "application/json")
        let resp = client.PostAsync(url, content).Result
        resp.Content.ReadAsStringAsync().Result

    let put (url: string) (body: string) =
        let content = new StringContent(body, Text.Encoding.UTF8, "application/json")
        let resp = client.PutAsync(url, content).Result
        resp.Content.ReadAsStringAsync().Result

    let delete (url: string) =
        let resp = client.DeleteAsync(url).Result
        resp.Content.ReadAsStringAsync().Result

    let httpHead (url: string) =
        let msg = new HttpRequestMessage(HttpMethod.Head, url)
        let resp = client.SendAsync(msg).Result
        (int resp.StatusCode, resp.Headers |> Seq.map (fun h -> h.Key, h.Value |> Seq.head) |> Seq.toArray)

    let getStatus (url: string) =
        let resp = client.GetAsync(url).Result
        int resp.StatusCode

    let download (url: string) (path: string) =
        let bytes = client.GetByteArrayAsync(url).Result
        File.WriteAllBytes(expand path, bytes)

    let curl (url: string) = sh (sprintf "curl -sL '%s'" url)

    // ─── html / dom ────────────────────────────────────────────────────
    // Lightweight HTML generation + parsing. No dependencies.
    //
    // Generate:  el "div" ["class","card"] [el "h1" [] [txt "hello"]] |> render
    // Parse:     htmlQuery "div.card" (text "page.html")
    // Extract:   htmlAttr "href" tag  /  htmlText tag

    type HtmlNode =
        | Element of tag: string * attrs: (string * string) list * children: HtmlNode list
        | Text of string
        | Raw of string

    let el tag attrs children = Element(tag, attrs, children)
    let txt s = Text s
    let raw s = Raw s

    let rec render (node: HtmlNode) =
        match node with
        | Text s -> System.Net.WebUtility.HtmlEncode(s)
        | Raw s -> s
        | Element(tag, attrs, children) ->
            let attrStr =
                attrs |> List.map (fun (k, v) -> sprintf " %s=\"%s\"" k (v.Replace("\"", "&quot;")))
                |> String.concat ""
            let inner = children |> List.map render |> String.concat ""
            match tag with
            | "br" | "hr" | "img" | "input" | "meta" | "link" ->
                sprintf "<%s%s/>" tag attrStr
            | _ -> sprintf "<%s%s>%s</%s>" tag attrStr inner tag

    let renderDoc (title: string) (head: HtmlNode list) (body: HtmlNode list) =
        "<!DOCTYPE html>\n" +
        render (el "html" ["lang","en"] [
            el "head" [] ([
                el "meta" ["charset","UTF-8"] []
                el "meta" ["name","viewport"; "content","width=device-width, initial-scale=1.0"] []
                el "title" [] [txt title]
            ] @ head)
            el "body" [] body
        ])

    // Parse: extract tags matching a simple CSS-like selector
    // Supports: tag, tag.class, tag#id, .class, #id
    let htmlQuery (selector: string) (html: string) =
        let pattern =
            if selector.StartsWith(".") then
                sprintf """<[a-z][a-z0-9]*[^>]*class\s*=\s*["'][^"']*%s[^"']*["'][^>]*/?>""" (selector.Substring(1))
            elif selector.StartsWith("#") then
                sprintf """<[a-z][a-z0-9]*[^>]*id\s*=\s*["']%s["'][^>]*/?>""" (selector.Substring(1))
            elif selector.Contains(".") then
                let parts = selector.Split('.')
                sprintf """<%s[^>]*class\s*=\s*["'][^"']*%s[^"']*["'][^>]*/?>""" parts.[0] parts.[1]
            elif selector.Contains("#") then
                let parts = selector.Split('#')
                sprintf """<%s[^>]*id\s*=\s*["']%s["'][^>]*/?>""" parts.[0] parts.[1]
            else
                sprintf """<%s[^>]*/?>""" selector
        Regex.Matches(html, pattern, RegexOptions.IgnoreCase)
        |> Seq.cast<Match> |> Seq.map (fun m -> m.Value) |> Seq.toArray

    // Extract text content between tags (greedy first match)
    let htmlInner (tag: string) (html: string) =
        match capture (sprintf """<%s[^>]*>(.*?)</%s>""" tag tag) html with
        | Some g -> g.[0]
        | None -> ""

    // Extract all text between matching tags
    let htmlInnerAll (tag: string) (html: string) =
        captureAll (sprintf """<%s[^>]*>(.*?)</%s>""" tag tag) html
        |> Array.map (fun g -> g.[0])

    // Extract attribute value from a tag string
    let htmlAttr (attr: string) (tag: string) =
        match capture (sprintf """%s\s*=\s*["']([^"']*)["']""" attr) tag with
        | Some g -> g.[0]
        | None -> ""

    // Strip all HTML tags, return plain text
    let htmlStrip (html: string) =
        sed @"<[^>]+>" "" html |> sed @"\s+" " " |> fun s -> s.Trim()

    // Build a simple HTML table from headers + rows
    let htmlTable (headers: string list) (rows: string list list) =
        el "table" [] (
            [el "thead" [] [el "tr" [] (headers |> List.map (fun h -> el "th" [] [txt h]))]] @
            [el "tbody" [] (rows |> List.map (fun row ->
                el "tr" [] (row |> List.map (fun c -> el "td" [] [txt c]))))]
        )

    // ─── js — F# S-expression DSL that emits JavaScript ────────────────
    //
    // F# discriminated unions ARE S-expressions.
    // Write DOM/Plotly/D3 logic in F#, emit clean JS.
    //
    //   let chart = Js.call "Plotly.newPlot" [Js.id "div"; Js.arr [...]]
    //   printfn "%s" (Js.emit chart)

    type Js =
        | JsNum of float
        | JsInt of int
        | JsStr of string
        | JsBool of bool
        | JsNull
        | JsId of string                           // variable/property reference
        | JsArr of Js list                          // [a, b, c]
        | JsObj of (string * Js) list               // {key: val, ...}
        | JsDot of Js * string                      // expr.prop
        | JsIdx of Js * Js                          // expr[idx]
        | JsCall of Js * Js list                    // func(args)
        | JsNew of string * Js list                 // new Ctor(args)
        | JsLet of string * Js                      // const name = expr
        | JsAssign of Js * Js                       // lhs = rhs
        | JsIf of Js * Js list * Js list            // if (cond) { ... } else { ... }
        | JsFor of string * Js * Js list            // for (const v of iterable) { ... }
        | JsForEach of Js * string * Js list        // arr.forEach(v => { ... })
        | JsArrow of string list * Js list          // (params) => { ... }
        | JsReturn of Js                            // return expr
        | JsFn of string * string list * Js list    // function name(params) { ... }
        | JsBinOp of string * Js * Js               // a op b
        | JsNot of Js                               // !expr
        | JsTernary of Js * Js * Js                 // cond ? a : b
        | JsRaw of string                           // verbatim JS
        | JsBlock of Js list                        // statement sequence
        | JsAwait of Js                             // await expr
        | JsAsync of string * string list * Js list // async function name(params) { ... }

    module Js =
        // Constructors — short names for fluent DSL
        let n (v: float) = JsNum v
        let i (v: int) = JsInt v
        let s (v: string) = JsStr v
        let b (v: bool) = JsBool v
        let nil = JsNull
        let id (name: string) = JsId name
        let arr items = JsArr items
        let obj pairs = JsObj pairs
        let dot expr prop = JsDot(expr, prop)
        let idx expr index = JsIdx(expr, index)
        let call fn args = JsCall(fn, args)
        let callN (name: string) args = JsCall(JsId name, args)
        let meth expr name args = JsCall(JsDot(expr, name), args)
        let newObj ctor args = JsNew(ctor, args)
        let letConst name expr = JsLet(name, expr)
        let assign lhs rhs = JsAssign(lhs, rhs)
        let ifElse cond thenBlock elseBlock = JsIf(cond, thenBlock, elseBlock)
        let ifThen cond thenBlock = JsIf(cond, thenBlock, [])
        let forOf v iterable body = JsFor(v, iterable, body)
        let forEach arr v body = JsForEach(arr, v, body)
        let arrow pars body = JsArrow(pars, body)
        let ret expr = JsReturn expr
        let fn name pars body = JsFn(name, pars, body)
        let op o a b = JsBinOp(o, a, b)
        let not' expr = JsNot expr
        let tern cond a b = JsTernary(cond, a, b)
        let raw js = JsRaw js
        let block stmts = JsBlock stmts
        let await' expr = JsAwait expr
        let asyncFn name pars body = JsAsync(name, pars, body)

        // DOM shortcuts
        let getElementById id = callN "document.getElementById" [s id]
        let querySelector sel = callN "document.querySelector" [s sel]
        let querySelectorAll sel = callN "document.querySelectorAll" [s sel]
        let createElement tag = callN "document.createElement" [s tag]
        let setInnerHTML el html = assign (dot el "innerHTML") html
        let setTextContent el t = assign (dot el "textContent") t
        let addClass el cls = meth (dot el "classList") "add" [s cls]
        let removeClass el cls = meth (dot el "classList") "remove" [s cls]
        let addEventListener el evt handler = meth el "addEventListener" [s evt; handler]
        let setStyle el prop v = assign (dot (dot el "style") prop) v
        let appendChild parent child = meth parent "appendChild" [child]

        // Plotly shortcuts
        let plotly divId traces layout =
            callN "Plotly.newPlot" [s divId; arr traces; layout]
        let trace pairs = obj pairs
        let scatterTrace x y extra =
            obj (["x", arr x; "y", arr y; "type", s "scatter"] @ extra)
        let barTrace x y extra =
            obj (["x", arr x; "y", arr y; "type", s "bar"] @ extra)
        let layout pairs = obj pairs

        // D3 shortcuts
        let d3select sel = callN "d3.select" [s sel]
        let d3selectAll sel = callN "d3.selectAll" [s sel]

        // Emit — render Js AST to JavaScript string
        let rec emit (node: Js) : string =
            match node with
            | JsNum v -> if v = System.Math.Floor(v) then sprintf "%.0f" v else sprintf "%g" v
            | JsInt v -> string v
            | JsStr v -> sprintf "'%s'" (v.Replace("\\", "\\\\").Replace("'", "\\'").Replace("\n", "\\n"))
            | JsBool true -> "true"
            | JsBool false -> "false"
            | JsNull -> "null"
            | JsId name -> name
            | JsArr items -> sprintf "[%s]" (items |> List.map emit |> String.concat ", ")
            | JsObj pairs ->
                let fields = pairs |> List.map (fun (k, v) -> sprintf "%s: %s" k (emit v))
                sprintf "{%s}" (fields |> String.concat ", ")
            | JsDot(expr, prop) -> sprintf "%s.%s" (emit expr) prop
            | JsIdx(expr, idx) -> sprintf "%s[%s]" (emit expr) (emit idx)
            | JsCall(fn, args) ->
                sprintf "%s(%s)" (emit fn) (args |> List.map emit |> String.concat ", ")
            | JsNew(ctor, args) ->
                sprintf "new %s(%s)" ctor (args |> List.map emit |> String.concat ", ")
            | JsLet(name, expr) -> sprintf "const %s = %s;" name (emit expr)
            | JsAssign(lhs, rhs) -> sprintf "%s = %s;" (emit lhs) (emit rhs)
            | JsIf(cond, thenBlock, []) ->
                sprintf "if (%s) {\n%s\n}" (emit cond) (emitBlock thenBlock)
            | JsIf(cond, thenBlock, elseBlock) ->
                sprintf "if (%s) {\n%s\n} else {\n%s\n}" (emit cond) (emitBlock thenBlock) (emitBlock elseBlock)
            | JsFor(v, iter, body) ->
                sprintf "for (const %s of %s) {\n%s\n}" v (emit iter) (emitBlock body)
            | JsForEach(arr, v, body) ->
                sprintf "%s.forEach(%s => {\n%s\n});" (emit arr) v (emitBlock body)
            | JsArrow([], [JsReturn expr]) -> sprintf "() => %s" (emit expr)
            | JsArrow([p], [JsReturn expr]) -> sprintf "%s => %s" p (emit expr)
            | JsArrow(pars, [JsReturn expr]) ->
                sprintf "(%s) => %s" (pars |> String.concat ", ") (emit expr)
            | JsArrow(pars, body) ->
                sprintf "(%s) => {\n%s\n}" (pars |> String.concat ", ") (emitBlock body)
            | JsReturn expr -> sprintf "return %s;" (emit expr)
            | JsFn(name, pars, body) ->
                sprintf "function %s(%s) {\n%s\n}" name (pars |> String.concat ", ") (emitBlock body)
            | JsBinOp(op, a, b) -> sprintf "(%s %s %s)" (emit a) op (emit b)
            | JsNot expr -> sprintf "!%s" (emit expr)
            | JsTernary(cond, a, b) -> sprintf "(%s ? %s : %s)" (emit cond) (emit a) (emit b)
            | JsRaw js -> js
            | JsBlock stmts -> emitBlock stmts
            | JsAwait expr -> sprintf "await %s" (emit expr)
            | JsAsync(name, pars, body) ->
                sprintf "async function %s(%s) {\n%s\n}" name (pars |> String.concat ", ") (emitBlock body)

        and emitBlock (stmts: Js list) : string =
            stmts |> List.map (fun s -> "  " + emit s) |> String.concat "\n"

        // Emit to file
        let emitToFile (path: string) (stmts: Js list) =
            let js = stmts |> List.map emit |> String.concat "\n"
            File.WriteAllText(expand path, js)
