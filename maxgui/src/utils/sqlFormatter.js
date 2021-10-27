//https://github.com/zeroturnaround/sql-formatter/blob/master/src/languages/MariaDbFormatter.jsu
import Formatter from 'sql-formatter/lib/core/Formatter'
import Tokenizer from 'sql-formatter/lib/core/Tokenizer'
import reservedWords from 'components/QueryEditor/reservedWords'

const reservedTopLevelWords = [
    'ALTER COLUMN',
    'ALTER TABLE',
    'DELETE FROM',
    'EXCEPT',
    'FROM',
    'GROUP BY',
    'HAVING',
    'INSERT INTO',
    'INSERT',
    'LIMIT',
    'ORDER BY',
    'SELECT',
    'UPDATE',
    'VALUES',
    'WHERE',
]

const reservedTopLevelWordsNoIndent = ['INTERSECT', 'INTERSECT ALL', 'UNION', 'UNION ALL']

const reservedNewlineWords = [
    'AND',
    'ELSE',
    'OR',
    'WHEN',
    // joins
    'JOIN',
    'INNER JOIN',
    'LEFT JOIN',
    'LEFT OUTER JOIN',
    'RIGHT JOIN',
    'RIGHT OUTER JOIN',
    'CROSS JOIN',
    'NATURAL JOIN',
    // non-standard joins
    'STRAIGHT_JOIN',
    'NATURAL LEFT JOIN',
    'NATURAL LEFT OUTER JOIN',
    'NATURAL RIGHT JOIN',
    'NATURAL RIGHT OUTER JOIN',
    'CHANGE COLUMN',
    'ADD COLUMN',
    'DROP COLUMN',
    'ADD UNIQUE INDEX',
    'DROP INDEX',
]

// For reference: https://mariadb.com/kb/en/sql-statements-structure/
class MariaDbFormatter extends Formatter {
    tokenizer() {
        return new Tokenizer({
            reservedWords,
            reservedTopLevelWords,
            reservedNewlineWords,
            reservedTopLevelWordsNoIndent,
            stringTypes: ['``', "''", '""'],
            openParens: ['(', 'CASE'],
            closeParens: [')', 'END'],
            indexedPlaceholderTypes: ['?'],
            namedPlaceholderTypes: [],
            lineCommentTypes: ['--', '#'],
            specialWordChars: ['@'],
            operators: [':=', '<<', '>>', '!=', '<>', '<=>', '&&', '||'],
        })
    }
}

export const format = (query, cfg = {}) => {
    return new MariaDbFormatter(cfg).format(query)
}
