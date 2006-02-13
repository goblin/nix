#include "get-drvs.hh"
#include "nixexpr-ast.hh"


typedef set<Expr> Exprs;


static bool getDerivation(EvalState & state, Expr e,
    DrvInfos & drvs, Exprs & doneExprs)
{
    ATermList es;
    e = evalExpr(state, e);
    if (!matchAttrs(e, es)) return false;

    ATermMap attrs;
    queryAllAttrs(e, attrs, false);
    
    Expr a = attrs.get("type");
    if (!a || evalString(state, a) != "derivation") return false;

    /* Remove spurious duplicates (e.g., an attribute set like `rec {
       x = derivation {...}; y = x;}'. */
    if (doneExprs.find(e) != doneExprs.end()) return true;
    doneExprs.insert(e);

    DrvInfo drv;
    
    a = attrs.get("name");
    if (!a) throw badTerm("derivation name missing", e);
    drv.name = evalString(state, a);

    a = attrs.get("system");
    if (!a)
        drv.system = "unknown";
    else
        drv.system = evalString(state, a);

    drv.attrs = attrs;

    drvs.push_back(drv);
    return true;
}


bool getDerivation(EvalState & state, Expr e, DrvInfo & drv)
{
    Exprs doneExprs;
    DrvInfos drvs;
    bool result = getDerivation(state, e, drvs, doneExprs);
    if (result) drv = drvs.front();
    return result;
}


static void getDerivations(EvalState & state, Expr e,
    DrvInfos & drvs, Exprs & doneExprs, const string & attrPath)
{
    /* Automatically call functions that have defaults for all
       arguments. */
    ATermList formals;
    ATerm body, pos;
    if (matchFunction(e, formals, body, pos)) {
        for (ATermIterator i(formals); i; ++i) {
            Expr name, def;
            if (matchNoDefFormal(*i, name))
                throw Error(format("expression evaluates to a function with no-default arguments (`%1%')")
                    % aterm2String(name));
            else if (!matchDefFormal(*i, name, def))
                abort(); /* can't happen */
        }
        getDerivations(state,
            makeCall(e, makeAttrs(ATermMap())),
            drvs, doneExprs, attrPath);
        return;
    }

    /* Parse the start of attrPath. */
    enum { apNone, apAttr, apIndex } apType;
    string attrPathRest;
    string attr;
    int attrIndex;
    Error attrError =
        Error(format("attribute selection path `%1%' does not match expression") % attrPath);

    if (attrPath.empty())
        apType = apNone;
    else {
        string::size_type dot = attrPath.find(".");
        if (dot == string::npos) {
            attrPathRest = "";
            attr = attrPath;
        } else {
            attrPathRest = string(attrPath, dot + 1);
            attr = string(attrPath, 0, dot);
        }
        apType = apAttr;
        if (string2Int(attr, attrIndex)) apType = apIndex;
    }

    /* Process the expression. */
    ATermList es;
    DrvInfo drv;

    e = evalExpr(state, e);

    if (getDerivation(state, e, drvs, doneExprs)) {
        if (apType != apNone) throw attrError;
        return;
    }

    if (matchAttrs(e, es)) {
        if (apType != apNone && apType != apAttr) throw attrError;
        ATermMap drvMap;
        queryAllAttrs(e, drvMap);
        if (apType == apNone) {
            for (ATermIterator i(drvMap.keys()); i; ++i) {
                debug(format("evaluating attribute `%1%'") % aterm2String(*i));
                getDerivation(state, drvMap.get(*i), drvs, doneExprs);
            }
        } else {
            Expr e2 = drvMap.get(attr);
            if (!e2) throw Error(format("attribute `%1%' in selection path not found") % attr);
            debug(format("evaluating attribute `%1%'") % attr);
            getDerivation(state, e2, drvs, doneExprs);
            if (!attrPath.empty())
                getDerivations(state, e2, drvs, doneExprs, attrPathRest);
        }
        return;
    }

    if (matchList(e, es)) {
        if (apType != apNone && apType != apIndex) throw attrError;
        if (apType == apNone) {
            for (ATermIterator i(es); i; ++i) {
                debug(format("evaluating list element"));
                if (!getDerivation(state, *i, drvs, doneExprs))
                    getDerivations(state, *i, drvs, doneExprs, attrPathRest);
            }
        } else {
            Expr e2 = ATelementAt(es, attrIndex);
            if (!e2) throw Error(format("list index %1% in selection path not found") % attrIndex);
            debug(format("evaluating list element"));
            if (!getDerivation(state, e2, drvs, doneExprs))
                getDerivations(state, e2, drvs, doneExprs, attrPathRest);
        }
        return;
    }

    throw Error("expression does not evaluate to a derivation (or a set or list of those)");
}


void getDerivations(EvalState & state, Expr e, DrvInfos & drvs,
    const string & attrPath)
{
    Exprs doneExprs;
    getDerivations(state, e, drvs, doneExprs, attrPath);
}