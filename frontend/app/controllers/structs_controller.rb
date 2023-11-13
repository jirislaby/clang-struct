class StructsController < ApplicationController
  def index
    @structs = MyStruct
    if params[:filter] != ''
      @filter = "%#{params[:filter]}%"
      @structs = @structs.where('struct.name LIKE ?', @filter)
    end
    if params[:filter_file] != ''
      @filter = "%#{params[:filter_file]}%"
      @structs = @structs.where('source.src LIKE ?', @filter)
    end
    @structs = @structs.joins(:source).limit(500)
    @structs_count = @structs.count
    @structs = @structs.select('struct.*', 'source.src AS src_file').
      order('src_file, struct.begLine')

    respond_to do |format|
      format.html
    end
  end

  def show
    @struct = MyStruct.joins(:source).select('struct.*', 'source.src AS src_file').find(params[:id])
    @members = Member.select('member.*',
                             "(SELECT id FROM struct AS nested " <<
                             "WHERE nested.src = #{@struct.src} AND " <<
                             "member.begLine == nested.begLine AND " <<
                             "member.begCol == nested.begCol LIMIT 1) " <<
                             "AS nested_id").
      where(struct: @struct)

    respond_to do |format|
      format.html
    end
  end

end
