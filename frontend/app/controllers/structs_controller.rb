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
    @structs = @structs.joins(:source).order('source.src, struct.begLine').limit(500);

    respond_to do |format|
      format.html
    end
  end

  def show
    @struct = MyStruct.joins(:source, :member).find(params[:id])

    respond_to do |format|
      format.html
    end
  end

end
