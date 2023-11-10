# This file is auto-generated from the current state of the database. Instead
# of editing this file, please use the migrations feature of Active Record to
# incrementally modify your database, and then regenerate this schema definition.
#
# This file is the source Rails uses to define your schema when running `bin/rails
# db:schema:load`. When creating a new database, `bin/rails db:schema:load` tends to
# be faster and is potentially less error prone than running all of your
# migrations from scratch. Old migrations may fail to apply correctly if those
# migrations use external dependencies or application code.
#
# It's strongly recommended that you check this file into your version control system.

ActiveRecord::Schema[7.1].define(version: 0) do
  create_table "member", force: :cascade do |t|
    t.text "name", null: false
    t.integer "struct", null: false
    t.integer "begLine", null: false
    t.integer "begCol", null: false
    t.integer "endLine"
    t.integer "endCol"
  end

  create_table "source", force: :cascade do |t|
    t.text "src", null: false
  end

  create_table "struct", force: :cascade do |t|
    t.text "name", null: false
    t.text "attrs"
    t.integer "src", null: false
    t.integer "begLine", null: false
    t.integer "begCol", null: false
    t.integer "endLine"
    t.integer "endCol"
  end

  create_table "use", force: :cascade do |t|
    t.integer "member", null: false
    t.integer "src", null: false
    t.integer "begLine", null: false
    t.integer "begCol", null: false
    t.integer "endLine"
    t.integer "endCol"
    t.integer "load"
  end

  add_foreign_key "member", "struct", column: "struct"
  add_foreign_key "struct", "source", column: "src"
  add_foreign_key "use", "member", column: "member"
  add_foreign_key "use", "source", column: "src"
end
