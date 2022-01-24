describe("parsing deeply nested expressions", () => {
    test("parenthesis", () => {
        const opening = Array(1024).join("(");
        const closing = opening.replaceAll("(", ")");
        expect(opening + "0" + closing).toEval();
    });

    test("calls", () => {
        const opening = Array(1024).join("noop(");
        const closing = opening.replaceAll("noop(", ")");
        expect(opening + "0" + closing).toEval();
    });

    test("arrays", () => {
        const opening = Array(1024).join("[");
        const closing = opening.replaceAll("[", "]");
        expect(opening + closing).toEval();
    });

    test("objects", () => {
        const opening = Array(1024).join("{ x: ");
        const closing = opening.replaceAll("{ x: ", " }");
        expect(`(${opening} null ${closing})`).toEval();
    });

    test("conditions", () => {
        const opening = Array(1024).join("x ? ");
        const closing = opening.replaceAll("x ? ", "y : ");
        expect(`${opening}${closing}null`).toEval();
    });

    test("unbalanced parenthesis", () => {
        const opening = Array(1024).join("(");
        const closing = opening.replaceAll("(", ")");
        expect(opening + "(0" + closing).not.toEval(); // Note: missing a closing paren
        expect(opening).not.toEval(); // Note: missing closing parens
    });

    test("unbalanced arrays", () => {
        const opening = Array(1024).join("[");
        const closing = opening.replaceAll("[", "]");
        expect(opening + "[0" + closing).not.toEval(); // Note: missing a closing bracket
        expect(opening).not.toEval(); // Note: missing closing brackets
    });

    test("unbalanced objects", () => {
        const opening = Array(1024).join("{ x: ");
        const closing = opening.replaceAll("{ x: ", " }");
        expect("(" + opening + "{0" + closing + ")").not.toEval(); // Note: missing a closing brace
        expect("(" + opening + "[0" + closing + ")").not.toEval(); // Note: missing a closing bracket
        expect("(" + opening + ")").not.toEval(); // Note: missing closing braces
    });

    test("assignments", () => {
        const expressions = Array(1024).join("x = ");
        expect(`${expressions}null`).toEval();
    });

    test("operators", () => {
        const OPS = {
            unary: ["+ ", "- ", "void "],
            binary: [" + ", " - ", " * ", " / "],
        };
        for (const unary of OPS.unary) {
            const unaryOps = Array(64).join(unary);
            for (const binary of OPS.binary) {
                const rhs = unaryOps + "0";
                const lhs = unaryOps + "0";
                const binaryOps = Array(64).join(binary + rhs);
                expect(lhs + binaryOps).toEval();
            }
        }
    });

    test("operators used in non-operator positions", () => {
        // for...in
        const expression = Array(512).join(" x in");
        const forExpression = `for(const x in ${expression} y);`;
        // Should not mistake the first 'in' as an operator!
        const opening = Array(512).join("{");
        const closing = opening.replaceAll("{", "}");
        expect(`${opening}${forExpression}${closing}`).toEval();
    });

    test("unbalanced conditions", () => {
        const opening = Array(1024).join("x ? ");
        const closing = opening.replaceAll("x ? ", "y : ");
        expect(`${opening}${closing}`).not.toEval(); // Note: missing the final expression
        expect(`${opening}${closing.replace("y : ", "(a)")}null`).not.toEval(); // Note: missing one "y :"
    });
});
