#!/bin/bash
#
# admin_ctl.sh - Admin account management script
#
# Usage:
#   ./sbin/admin_ctl.sh -c <conf_path> <command>
#
# Commands:
#   reset   Reset admin password to default (admin123)
#   show    Show admin username and account info
#   clean   Delete admin account
#
# Example:
#   ./sbin/admin_ctl.sh -c conf/register_student.conf clean
#

CONF_PATH=""
DEFAULT_PWD="admin123"

usage() {
    echo "Usage: $0 -c <conf_path> {reset|show|clean}"
    echo ""
    echo "Options:"
    echo "  -c <conf_path>  Path to register_student.conf"
    echo ""
    echo "Commands:"
    echo "  reset   Reset admin password to default ($DEFAULT_PWD)"
    echo "  show    Show admin username and account info"
    echo "  clean   Delete admin account from database"
    exit 1
}

# Parse arguments
while getopts ":c:" opt; do
    case $opt in
        c) CONF_PATH="$OPTARG" ;;
        \?) usage ;;
        :) echo "Error: option -$OPTARG requires an argument"; usage ;;
    esac
done
shift $((OPTIND - 1))

COMMAND="$1"

if [ -z "$CONF_PATH" ] || [ -z "$COMMAND" ]; then
    usage
fi

if [ ! -f "$CONF_PATH" ]; then
    echo "Error: config file not found: $CONF_PATH"
    exit 1
fi

if ! command -v python3 &>/dev/null; then
    echo "Error: python3 command not found"
    exit 1
fi

# Extract db path from INI config and execute the requested command
python3 - "$CONF_PATH" "$COMMAND" "$DEFAULT_PWD" <<'PYEOF'
import sys
import os
import sqlite3
import hashlib
import configparser

conf_path = sys.argv[1]
command = sys.argv[2]
default_pwd = sys.argv[3]

# Parse INI config
config = configparser.ConfigParser()
config.read(conf_path)

if not config.has_section('db') or not config.has_option('db', 'path'):
    print('Error: [db] path not found in config')
    sys.exit(1)

db_path = config.get('db', 'path')

# Resolve relative path against project root (parent of conf/)
conf_dir = os.path.dirname(os.path.abspath(conf_path))
project_root = os.path.dirname(conf_dir)
if not os.path.isabs(db_path):
    db_path = os.path.join(project_root, db_path)

if not os.path.exists(db_path):
    print('Error: database not found: ' + db_path)
    sys.exit(1)

# Determine project root for chdir (parent of conf/)
project_root = os.path.dirname(conf_dir)
os.chdir(project_root)

db = sqlite3.connect(db_path)
row = db.execute('SELECT id, username FROM users WHERE role=0 LIMIT 1').fetchone()

if command == 'show':
    if not row:
        print('No admin account found.')
        sys.exit(1)
    admin_id, admin_name = row
    print('Admin account info:')
    print('  Database: ' + db_path)
    print('  ID:       ' + str(admin_id))
    print('  Username: ' + admin_name)
    print('  Password: (hashed, use reset to set a known password)')

elif command == 'reset':
    if not row:
        print('No admin account found.')
        sys.exit(1)
    admin_id, admin_name = row
    salt = os.urandom(16).hex()
    hash_val = hashlib.sha256((default_pwd + salt).encode()).hexdigest()
    db.execute('UPDATE users SET password_hash=?, salt=? WHERE id=?', (hash_val, salt, admin_id))
    db.commit()
    print('Admin password reset successfully.')
    print('  Database: ' + db_path)
    print('  Username: ' + admin_name)
    print('  Password: ' + default_pwd)

elif command == 'clean':
    if not row:
        print('No admin account found.')
        sys.exit(1)
    admin_id, admin_name = row
    db.execute('DELETE FROM users WHERE id=?', (admin_id,))
    db.commit()
    print('Admin account deleted: ' + admin_name + ' (id=' + str(admin_id) + ')')
    print('  Database: ' + db_path)

else:
    print('Unknown command: ' + command)
    sys.exit(1)

db.close()
PYEOF
