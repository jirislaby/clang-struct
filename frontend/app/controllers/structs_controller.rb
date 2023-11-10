class StructsController < ApplicationController
  def index
    @structs = MyStruct.joins(:source).order('source.src, struct.begLine').limit(100);

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
